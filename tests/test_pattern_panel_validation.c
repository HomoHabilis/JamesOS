#define FM1_PATTERN_PANEL_TEST 1
#define DINGHY_HOST 1
#ifdef PS_DEVICE_SAVE_TEST
#define DINGHY_DEVICE_SAVE_TEST 1
#else
#define DINGHY_EMULATOR_SAVE 1
#endif
#define BR_PATTERN_SONG_EXPERIMENT 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/pattern_song.c"
#include <assert.h>
fm1_pattern_song br_ps_state;
static unsigned selects, copies, pastes;
static int copy_cb(void *c, uint8_t from, uint8_t to) { (void)c; (void)from; (void)to; return 1; }
int br_ps_select(uint32_t p) { ++selects; return fm1_pattern_select(&br_ps_state, p, br_playing, copy_cb, 0); }
int br_ps_copy_pattern(uint32_t p) { ++copies; return p < FM1_PATTERN_COUNT; }
int br_ps_paste_pattern(uint32_t p) { ++pastes; return p < FM1_PATTERN_COUNT; }
int br_ps_set_entry(uint32_t pos,uint32_t p,uint32_t bars) { return fm1_song_entry_set(&br_ps_state,pos,p,bars); }
int br_ps_set_length(uint32_t len) { return fm1_song_length_set(&br_ps_state, len); }
int br_ps_enable_song(uint32_t on) { if (on) return fm1_song_start(&br_ps_state); br_ps_state.song_enabled=0; return 1; }
static void tick(void) { ++fm1_ms; br_app_tick(); }
static void down(uint32_t b) { fm1_in.buttons |= 1u<<b; host_pressed |= 1u<<b; tick(); }
static void up(uint32_t b) { fm1_in.buttons &= ~(1u<<b); host_released |= 1u<<b; tick(); }
static void key_down(uint32_t k) { fm1_in.notes |= 1u<<k; host_note_pressed |= 1u<<k; tick(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u<<k); tick(); }
int main(void) {
 static const unsigned black[11]={1,3,5,8,10,13,15,17,20,22,25};
 br_app_init(); fm1_pattern_song_init(&br_ps_state); br_ui.page=BR_PAGE_PS;
 for(unsigned i=0;i<11;i++){br_pattern_diag_key=1u<<black[i];tick();assert(br_ui.previous_keys==br_pattern_diag_key);br_pattern_diag_key=0;tick();assert(br_ps_state.active==i);}
 br_pattern_diag_key=1u<<1;tick();host_pressed|=1u<<BTN_REC;tick();host_released|=1u<<BTN_REC;tick();br_pattern_diag_key=0;tick();assert(copies==1);
 br_pattern_diag_key=1u<<3;tick();host_pressed|=1u<<BTN_PLAY;tick();host_released|=1u<<BTN_PLAY;tick();br_pattern_diag_key=0;tick();assert(pastes==1 && !br_playing);
 br_playing=1;br_pattern_diag_key=1u<<5;tick();br_pattern_diag_key=0;tick();assert(br_ps_state.active==10 && br_ps_state.queued==2);assert(fm1_pattern_song_bar(&br_ps_state)&&br_ps_state.active==2);
 br_playing=0;br_pattern_diag_key=1u<<8;tick();fm1_in.notes=1u<<0;tick();assert(!br_pattern_diag_key && br_ps_state.active==2);fm1_in.notes=0;tick();
 br_pattern_diag_key=1u<<10;tick();br_diagnostic_restore_in_progress=1;tick();assert(!br_pattern_diag_key);br_diagnostic_restore_in_progress=0;tick();assert(br_ps_state.active==2);
 return 0;
}
