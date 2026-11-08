#!/usr/bin/env python3
"""Interleave the real target project watch, accessor and end logic on the host."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'firmware/src/dinghy.c').read_text()
a=s.index('static int br_ps_project_input_idle(');b=s.index('\n#if defined(FM1_PATTERN_PROVISION_EXPERIMENT)',a);watch=s[a:b]
def extract(prefix):
 a=s.index(prefix);a=s.index(prefix,a+len(prefix)) if s[a:s.index('\n',a)].endswith(';') else a;q=s.index('{',a);depth=0
 for i in range(q,len(s)):
  depth+=(s[i]=='{')-(s[i]=='}')
  if not depth:return s[a:i+1]
 raise ValueError(prefix)
revoke=extract('static int br_ps_target_revoke(')
body='\n'.join(extract(x) for x in ('static int br_ps_target_project_exclusive(', 'br_pattern_park_io *br_ps_target_project_park(', 'int br_ps_target_project_end('))
shim=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include "pattern_loan_token.h"
#define FM1_NENC 7
#define BR_MIDI_ROUTE_NOTES 64
#define FELUCCA_OTA 1
#define BR_PARK_OK 0
static uint32_t recovery_active,fm1_cpu1_early_reboot_pending,br_playing,br_recording,br_hold_on,br_save_pending,br_midi_clock_active,mi_r,mi_w,sx_ready,sx_collect,sx_busy,so_r,so_w;
static uint32_t br_ps_project_scan_readonly,br_ps_project_scan_dirty;
static uint32_t br_ps_project_watch_token,br_ps_project_watch_panel,br_ps_project_watch_rx,br_ps_project_watch_midi,br_ps_project_watch_config,br_ps_project_watch_resets,br_ps_project_watch_usb_state;
static br_pattern_loan_token br_ps_target_project_token={.sequence=9,.active=9};
static uint32_t br_ps_target_worker_certified=1,br_diagnostic_restore_in_progress=1;
typedef struct {int x;} br_pattern_park_io;
static br_pattern_park_io br_ps_target_park_io;
static int enabled=1,inject,inject_resume,aborts,ends,loans=1,fence=1,holds,br_ps_project_post_release_fault;
static uint32_t br_ps_project_quarantine=1;
static unsigned br_ps_target_session_lent=1,br_ps_target_workspace_lent=1,br_ps_target_settings_lent=1,br_ps_target_scratch_capacity=4096;
static struct {uint32_t rx_pkts,rx_pend,config,resets,up,detached,suspended,ota_req,uboot_req,sx_on;} usb={.config=1,.up=1};
static struct {uint32_t notes,buttons,pressed,released,notes_pressed,pattern_activity;int16_t enc_steps[7];int8_t enc_sub[7];} fm1_in;
static struct {uint32_t held_note[27];} br_ui;static struct {struct {uint8_t channel;} note[64];} br_midi_routes;
static uint32_t irq_save(void){uint32_t old=enabled;enabled=0;return old;}
static void irq_restore(uint32_t old){enabled=old;if(enabled&&inject){fm1_in.pattern_activity++;inject=0;}}
static int br_pattern_session_scratch_blocked(void){return 0;}
static int br_ps_target_parked(void*x){(void)x;return fence;}
static int fm1_cpu1_worker_reset_held(void){return 0;}
static int fm1_cpu1_worker_abort_hold_reset(void){holds++;return 0;}
static int br_pattern_session_bind(void*x,int n){(void)x;(void)n;return 0;}
static void br_ps_settings_scratch_release(void){}
static void br_pattern_session_loan_release(void){}
static void br_ps_host_workspace_release(void){}
static int br_pattern_park_abort(br_pattern_park_io*x){(void)x;aborts++;return -1;}
static int br_ps_target_revoke(void *);
static int br_pattern_park_end(br_pattern_park_io*x){(void)x;ends++;if(br_ps_target_revoke(0)){aborts++;return -1;}loans=0;fence=0;if(inject_resume)fm1_in.pattern_activity++;return 0;}
static void br_quiescent_transaction_end(void){uint32_t old=irq_save();br_diagnostic_restore_in_progress=0;irq_restore(old);}
'''
tail=r'''
int main(void){
 usb.ota_req=1;assert(!br_ps_project_watch_begin(9));usb.ota_req=0;
 sx_ready=1;fm1_in.buttons=8;fm1_in.pressed=8;mi_w=1;assert(br_ps_project_watch_begin(9));
 assert(sx_ready&&fm1_in.buttons==8&&fm1_in.pressed==8&&mi_w==1);sx_ready=0;

 /* Synchronous write: interrupt-side pending input is left intact. */
 fm1_in.pattern_activity++;fm1_in.pressed=8;mi_w++;usb.rx_pkts++;usb.sx_on=1;sx_collect=1;
 assert(br_ps_project_watch_ok());assert(fm1_in.pressed==8&&mi_w==2&&usb.sx_on&&sx_collect);
 usb.ota_req=1;usb.uboot_req=1;assert(br_ps_project_watch_ok());usb.ota_req=usb.uboot_req=0;
 usb.resets++;usb.config=0;usb.detached=1;usb.up=0;assert(br_ps_project_watch_ok());
 usb.resets--;usb.config=1;usb.detached=0;usb.up=1;
 usb.sx_on=0;sx_collect=0;fm1_in.pressed=0;mi_r=mi_w;
 assert(br_ps_target_project_end(9));assert(!br_diagnostic_restore_in_progress&&!fence&&!loans&&!aborts);
 /* Read-only scan: queued traffic survives clean end. */
 br_ps_target_project_token=(br_pattern_loan_token){.sequence=10,.active=10};
 br_diagnostic_restore_in_progress=1;br_ps_project_quarantine=1;loans=fence=1;
 br_ps_target_session_lent=br_ps_target_workspace_lent=br_ps_target_settings_lent=1;
 assert(br_ps_project_watch_begin(10));mi_w++;usb.rx_pkts++;fm1_in.released=4;
 assert(br_ps_project_watch_ok());
 assert(br_ps_target_project_end(10));assert(mi_w==3&&fm1_in.released==4&&!fence&&!loans&&!aborts);
 /* Lost worker/fence still quarantines: no loans are released. */
 br_ps_target_project_token=(br_pattern_loan_token){.sequence=11,.active=11};
 br_diagnostic_restore_in_progress=1;br_ps_project_quarantine=1;loans=1;fence=1;
 br_ps_target_session_lent=br_ps_target_workspace_lent=br_ps_target_settings_lent=1;
 assert(br_ps_project_watch_begin(11));fence=0;
 assert(!br_ps_target_project_end(11));assert(loans&&br_diagnostic_restore_in_progress&&aborts);
 puts("deferred input and semantic ownership checks passed");return 0;}
'''
with tempfile.TemporaryDirectory(prefix='fm1-project-watch-') as d:
 p=Path(d);(p/'probe.c').write_text(shim+watch+revoke+body+tail)
 subprocess.run(['cc','-std=c11','-O1','-g','-fsanitize=address,undefined',f'-I{root / "firmware/src"}',str(p/'probe.c'),'-o',str(p/'probe')],check=True)
 subprocess.run([str(p/'probe')],check=True)
