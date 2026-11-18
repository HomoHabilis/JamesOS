/* SPDX-License-Identifier: GPL-3.0-only */
/* Version-5 projects on a unit (host; real project-store code and the real boot
 * recovery screen path, never hardware). fixtures/project-v5-store.bin holds
 * sectors 0-1 (named slot 1, "V5 SONG") and 24-25 (the working project W00) as
 * b9ef12f (FMP1 envelope 5, session v17) wrote them: six used patterns, pattern
 * 3 active and edited, a non-default session. fixtures/project-v5-loaded.bin is
 * what b9ef12f restores from it (session, owner bank, live pattern);
 * fixtures/gen_v5_store.c.txt is the generator (built in that tree). */
#define DINGHY_DEVICE_SAVE_TEST 1
#define FM1_PATTERN_TARGET_EXPERIMENT 1
#define FM1_PATTERN_PROJECT_TARGET_EXPERIMENT 1
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "pattern_boot_recovery.h"
#include <stdio.h>
/* The boot/SAVE screen's target backend, over the real fixed runtime. */
int br_pp_target_scan(unsigned s, char name[13]) { return fm1_fixed_runtime_scan(&runtime, s, name); }
int br_pp_target_save(unsigned s, const char *name) { return fm1_fixed_bridge_save(&runtime.bridge, s, name); }
int br_pp_target_load(unsigned s) { return fm1_fixed_bridge_load(&runtime.bridge, s); }
int br_pp_target_ready(void) { return 1; }
int br_pp_target_recover(int *slot) { int rc = fm1_fixed_bridge_recover(&runtime.bridge); *slot = rc == PFS_OK ? selected_slot : -1; return rc; }
int br_pp_target_init_project(void) { return PFS_INVALID; }
static uint8_t expect[16384], got[16384];
static size_t expect_n;
int test_migrate_v17_session(uint8_t *record, size_t n);
static size_t dump(uint8_t *out)
{
    static br_session_t s; static br_pattern_bank v; static br_pattern_snapshot live; fm1_pattern_song song;
    size_t n = 0; uint16_t used; uint32_t x;
    assert(br_capture(&s)); memcpy(out, &s, sizeof s); n += sizeof s;
    assert(br_ps_host_export(&v, &live, &song));
    used = v.used; memcpy(out + n, &used, 2); n += 2; out[n++] = v.active;
    x = (uint32_t)v.size; memcpy(out + n, &x, 4); n += 4;
    for (unsigned i = 0; i < 11u; i++) {
        x = (uint32_t)v.offset[i]; memcpy(out + n, &x, 4); n += 4;
        x = (uint32_t)v.length[i]; memcpy(out + n, &x, 4); n += 4;
    }
    memcpy(out + n, v.arena, v.size); n += v.size;
    memcpy(out + n, &live, sizeof live); n += sizeof live;
    return n;
}
static void expect_loaded(const char *what)
{
    size_t n = dump(got);
    if (n != expect_n || memcmp(got, expect, n)) {
        size_t i = 0; while (i < n && i < expect_n && got[i] == expect[i]) ++i;
        fprintf(stderr, "%s: differs from b9ef12f at byte %zu (%zu vs %zu bytes)\n", what, i, n, expect_n); assert(0);
    }
    assert(!br_ps_state.song_length && !br_ps_state.loop_count && br_ps_state.mode == FM1_SONG_MODE_PATTERN &&
           br_ps_state.end == FM1_SONG_END_STOP && br_ps_state.active == 3u && br_ps_state.used == 0x3fu);
}
int main(void)
{
    fm1_fixed_runtime_port port = {0}; char name[PS_NAME_BYTES]; FILE *f;
    port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
    init_runtime_fixture();
    f = fopen("tests/fixtures/project-v5-store.bin", "rb"); assert(f);
    assert(fread(pair_flash, 1, 2u * PS_SECTOR, f) == 2u * PS_SECTOR);
    assert(fread(pair_flash + 24u * PS_SECTOR, 1, 2u * PS_SECTOR, f) == 2u * PS_SECTOR);
    fclose(f);
    f = fopen("tests/fixtures/project-v5-loaded.bin", "rb"); assert(f);
    expect_n = fread(expect, 1, sizeof expect, f); fclose(f); assert(expect_n > sizeof(br_session_t));
    /* This historical fixture uses 90s sample kit ID 50. Its sequence and all
     * unrelated session fields stay byte-identical; the kit is migrated to
     * the new 90s synthesized patch. */
    assert(expect[412] == BR_KIT_909);
    expect[412] = BR_KIT_PLAITS; expect[413] = 76u; expect[414] = 82u;
    expect[415] = 70u; expect[417] = 0u; expect[422] = 78u; expect[541] = 92u;
    /* The historical session is v17; current captures are v18.  Migrate the
     * fixture independently with the standalone codec before comparing the
     * full session and pattern state. */
    {
        assert(test_migrate_v17_session(expect, sizeof(br_session_t)));
    }
    memcpy(pair_before, pair_flash, sizeof pair_flash);
    assert(fm1_fixed_runtime_bind(&runtime, &port) == PFS_OK);
    br_bpm = 60u;
    /* Boot, through the screen's recovery: the v5 W00 loads (no "OLD PROJECT NOT
     * LOADED"), exactly as b9ef12f loads it, and nothing is written. */
    br_ui.message[0] = 0; es_project_boot_state = 0u;
    assert(br_project_target_recover() == PFS_OK);
    assert(strstr(br_ui.message, "90S KIT NOW SYNTH"));
    assert(es_project_boot_state == 1u && !fm1_project_old_format_seen && strcmp(br_ui.message, "OLD PROJECT NOT LOADED"));
    { br_pattern_boot_recovery b = {0}; assert(br_pattern_boot_result(&b, 100, PFS_OK, 0) == 1); }
    assert(br_bpm == 97u);
    expect_loaded("boot recovery");
    assert(!memcmp(pair_before, pair_flash, sizeof pair_flash));
    /* Slot scan: a normal project (not OLD FORMAT). */
    fm1_project_old_format_seen = 0u;
    assert(fm1_fixed_runtime_scan(&runtime, 0, name) == PFS_OK && !strcmp(name, "V5 SONG") && !fm1_project_old_format_seen);
    /* LOAD of the v5 slot: the same state, nothing written. */
    br_bpm = 61u; assert(fm1_arr_insert(&br_ps_state, 0, 1, 2) == FM1_ARR_OK);
    assert(fm1_fixed_bridge_load(&runtime.bridge, 0) == PFS_OK);
    expect_loaded("slot load");
    /* The named slot is untouched. LOAD rewrites the working project W00 from
     * the loaded state (as for any project: it records what is loaded), so W00
     * is now version 6 - the ordinary save path, not a conversion. */
    assert(!memcmp(pair_before, pair_flash, 24u * PS_SECTOR));
    for (unsigned i = 26u * PS_SECTOR; i < sizeof pair_flash; ++i) assert(pair_flash[i] == pair_before[i]);
    assert(memcmp(pair_before + 24u * PS_SECTOR, pair_flash + 24u * PS_SECTOR, 2u * PS_SECTOR));
    assert(br_project_target_recover() == PFS_OK);
    expect_loaded("boot after load");
    /* SAVE writes version 6: an arrangement now survives a reload. */
    assert(fm1_arr_insert(&br_ps_state, 0, 2, 3) == FM1_ARR_OK && fm1_arr_insert(&br_ps_state, 1, 5, 1) == FM1_ARR_OK);
    assert(fm1_arr_set_mode(&br_ps_state, FM1_SONG_MODE_SONG) == FM1_ARR_OK);
    assert(fm1_fixed_bridge_save(&runtime.bridge, 0, "V6 SONG") == PFS_OK);
    assert(fm1_arr_clear(&br_ps_state) == FM1_ARR_OK && fm1_arr_set_mode(&br_ps_state, 0) == FM1_ARR_OK);
    assert(fm1_fixed_bridge_load(&runtime.bridge, 0) == PFS_OK);
    assert(br_ps_state.song_length == 2u && br_ps_state.entry[0].pattern == 2u && br_ps_state.entry[1].pattern == 5u &&
           br_ps_state.mode == FM1_SONG_MODE_SONG && br_ps_state.active == 3u && br_bpm == 97u);
    assert(fm1_arr_clear(&br_ps_state) == FM1_ARR_OK);
    assert(br_project_target_recover() == PFS_OK && br_ps_state.song_length == 2u);
    puts("v5 project: boot recovery and LOAD restore it as b9ef12f did (empty arrangement); boot writes nothing; SAVE writes v6");
    return 0;
}
