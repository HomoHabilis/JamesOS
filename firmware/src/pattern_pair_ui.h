/* Host-only Pattern save-screen routing; not a target storage policy. */
#ifndef FM1_PATTERN_PAIR_UI_H
#define FM1_PATTERN_PAIR_UI_H
#if !defined(BR_PATTERN_PROJECT_STREAM_HOST) || !BR_PATTERN_PROJECT_STREAM_HOST || (defined(FM1_DEVICE) && FM1_DEVICE)
#error "Pattern stream UI routing is host-only until target integration is qualified"
#endif
#include "pattern_fixed_target.h"
int br_pair_ui_bind(fm1_fixed_target *);
int br_pair_ui_scan(unsigned,char [PS_NAME_BYTES]);
int br_pair_ui_save(unsigned,const char *);
int br_pair_ui_load(unsigned);
int br_pair_ui_init_project(void);
int br_pair_ui_recover(int *);
#endif
