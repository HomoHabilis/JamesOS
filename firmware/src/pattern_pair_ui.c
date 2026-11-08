#include "pattern_pair_ui.h"
static fm1_fixed_target *ui_target;
/* One binding per host process; do not substitute a faulted/live backend. */
int br_pair_ui_bind(fm1_fixed_target *target)
{ if(ui_target || !fm1_fixed_target_ready(target)) return PFS_INVALID;ui_target=target;return PFS_OK; }
int br_pair_ui_scan(unsigned slot,char name[PS_NAME_BYTES])
{ return ui_target?fm1_fixed_target_scan(ui_target,slot,name):PFS_INVALID; }
int br_pair_ui_save(unsigned slot,const char *name)
{ return ui_target?fm1_fixed_target_save(ui_target,slot,name):PFS_INVALID; }
int br_pair_ui_load(unsigned slot)
{ return ui_target?fm1_fixed_target_load(ui_target,slot):PFS_INVALID; }
/* Factory-init semantics need their own atomic working-record transaction. */
int br_pair_ui_init_project(void) { return PFS_INVALID; }
int br_pair_ui_recover(int *slot)
{ return ui_target?fm1_fixed_target_recover(ui_target,slot):PFS_INVALID; }
