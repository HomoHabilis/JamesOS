/* SPDX-License-Identifier: GPL-3.0-only */
/* Old-format projects on a unit (host; real project-store code, never hardware).
 * fixtures/project-v16-store.bin holds sectors 0-1 (named slot 1, "OLD SONG") and
 * 24-25 (the working project W00) as firmware 9b7e4eb (session v16, FMP1
 * envelope 4) wrote them after restoring the owner's capture 10-S1 and saving
 * slot 1; fixtures/gen_v16_store.c.txt is the generator (built in that tree). */
#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "pattern_boot_recovery.h"
#include <stdio.h>
static void load_image(void)
{
    FILE *f = fopen("tests/fixtures/project-v16-store.bin", "rb");
    assert(f);
    assert(fread(pair_flash, 1, 2u * PS_SECTOR, f) == 2u * PS_SECTOR);
    assert(fread(pair_flash + 24u * PS_SECTOR, 1, 2u * PS_SECTOR, f) == 2u * PS_SECTOR);
    fclose(f);
}
int main(void)
{
    fm1_fixed_runtime_port port = {0}; char name[PS_NAME_BYTES];
    static br_session_t before, after;
    port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
    init_runtime_fixture(); load_image(); memcpy(pair_before, pair_flash, sizeof pair_flash);
    assert(fm1_fixed_runtime_bind(&runtime, &port) == PFS_OK);
    assert(br_capture(&before));
    /* Boot: the old W00 is an old project, not a failed recovery. The unit keeps
     * its defaults and the store stays usable (FOREIGN_WORKING, not INVALID). */
    fm1_project_old_format_seen = 0u;
    assert(fm1_fixed_bridge_recover(&runtime.bridge) == FM1_PROJECT_FOREIGN_WORKING);
    assert(fm1_project_old_format_seen);
    { br_pattern_boot_recovery b = {0}; assert(br_pattern_boot_result(&b, 100, FM1_PROJECT_FOREIGN_WORKING, 0) == 1); }
    assert(br_capture(&after) && !memcmp(&before, &after, sizeof before));
    assert(!memcmp(pair_before, pair_flash, sizeof pair_flash));
    /* Slot scan: rejected, and marked as an old format (UI: OLD FORMAT). */
    fm1_project_old_format_seen = 0u;
    assert(fm1_fixed_runtime_scan(&runtime, 0, name) == PFS_INVALID && fm1_project_old_format_seen);
    fm1_project_old_format_seen = 0u;
    assert(fm1_fixed_runtime_scan(&runtime, 1, name) == PFS_EMPTY && !fm1_project_old_format_seen);
    /* LOAD of the old slot is refused; unit state and flash untouched. */
    assert(fm1_fixed_bridge_load(&runtime.bridge, 0) != PFS_OK);
    assert(br_capture(&after) && !memcmp(&before, &after, sizeof before));
    assert(!memcmp(pair_before, pair_flash, sizeof pair_flash));
    /* SAVE over it (after the UI's confirm) replaces it; it reloads. */
    br_bpm = 133u; br_part[0].sound.filter_env_depth = 21; assert(br_capture(&before));
    assert(fm1_fixed_bridge_save(&runtime.bridge, 0, "NEW SONG") == PFS_OK);
    assert(fm1_fixed_runtime_scan(&runtime, 0, name) == PFS_OK && !strcmp(name, "NEW SONG"));
    assert(!memcmp(pair_before + 2u * PS_SECTOR, pair_flash + 2u * PS_SECTOR, 22u * PS_SECTOR));
    br_bpm = 90u; br_part[0].sound.filter_env_depth = 0;
    assert(fm1_fixed_bridge_load(&runtime.bridge, 0) == PFS_OK);
    assert(br_capture(&after) && !memcmp(&before, &after, sizeof before));
    assert(br_bpm == 133u && br_part[0].sound.filter_env_depth == 21);
    /* The working project (rewritten by SAVE) now recovers normally. */
    assert(fm1_fixed_bridge_recover(&runtime.bridge) == PFS_OK);
    puts("old format: W00 boots to defaults with the store usable; old slot refused unchanged; SAVE replaces and reloads");
    return 0;
}
