#include <stdint.h>
#define DINGHY_HOST 1
#define FM1_PATTERN_V16_ONLY 1
#define FM1_PATTERN_DEVICE_CANDIDATE 1
#define BR_PATTERN_SESSION_LOAN_EXPERIMENT 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include <assert.h>
#include <stdio.h>
static int test_exclusive(void *ctx){return *(int *)ctx;}
int main(void){static const uint32_t legacy_lengths[]={240,240,900,908,916,920,952,960,964};br_session_t current,old,before,after;uint8_t saved[sizeof host_store];uint32_t saved_len,version;
 br_app_init();br_bpm=121;br_midi_channels[0]=0u;br_midi_channels[6]=0u;assert(br_capture(&current)&&current.v3.version==16&&br_session_v16_valid(&current));
 assert(!settings_save(&current,sizeof current));br_bpm=77;br_midi_channels[0]=br_midi_channels[6]=1u;br_load_session();assert(!br_session_save_blocked()&&br_bpm==121&&br_midi_channels[0]==0u&&br_midi_channels[6]==0u);
 assert(br_capture(&before));
 for(version=1;version<=9;version++){
  old=current;old.v3.version=version;assert(!settings_save(&old,legacy_lengths[version-1]));
  memcpy(saved,host_store,sizeof saved);saved_len=host_store_len;br_load_session();
  assert(br_session_save_blocked());assert(br_capture(&after)&&!memcmp(&before,&after,sizeof before));
  br_save_pending=1;br_save_poll();assert(!br_save_pending&&host_store_len==saved_len&&!memcmp(saved,host_store,sizeof saved));
 }
 for(version=10;version<=15;version++){
  old=current;old.v3.version=version;
  assert(!br_restore(&old));assert(!settings_save(&old,version==10?sizeof(br_session_v10_t):sizeof old));
  memcpy(saved,host_store,sizeof saved);saved_len=host_store_len;br_load_session();
  assert(br_session_save_blocked());assert(br_capture(&after)&&!memcmp(&before,&after,sizeof before));
  br_save_pending=1;br_save_poll();assert(!br_save_pending&&host_store_len==saved_len&&!memcmp(saved,host_store,sizeof saved));
 }
 /* Unknown/truncated settings stay protected and never reset the live take. */
 old=current;old.v3.version=99;assert(!settings_save(&old,sizeof old));memcpy(saved,host_store,sizeof saved);br_load_session();
 assert(br_session_save_blocked()&&br_capture(&after)&&!memcmp(&before,&after,sizeof before)&&!memcmp(saved,host_store,sizeof saved));
 /* Clock receive and clock send together are not a valid setting. */
 old=current;old.kit_retrig[3][2]|=0xc0u;assert(!br_session_v16_valid(&old));assert(!settings_save(&old,sizeof old));memcpy(saved,host_store,sizeof saved);br_load_session();
 assert(br_session_save_blocked()&&br_capture(&after)&&!memcmp(&before,&after,sizeof before)&&!memcmp(saved,host_store,sizeof saved));
 /* A blank-looking header does not make programmed payload safe to erase. */
 memset(host_store,0,sizeof host_store);host_store_len=0;host_store[100]=0x7e;memcpy(saved,host_store,sizeof saved);br_load_session();
 assert(br_session_save_blocked()&&br_capture(&after)&&!memcmp(&before,&after,sizeof before));br_save_pending=1;br_save_poll();assert(!memcmp(saved,host_store,sizeof saved));
 { void *loaded=0,*pending=0,*undo=0;size_t bytes=0;int exclusive=1;
   br_undo_valid=1;memset(&br_undo,0x6b,sizeof br_undo);
   assert(br_session_save_blocked()&&!br_pattern_session_scratch_blocked());
   assert(br_pattern_session_loan_acquire(3,test_exclusive,&exclusive,&loaded,&pending,&undo,&bytes));
   assert(loaded==&br_loaded&&pending==&br_pending_save&&!undo&&bytes==sizeof(br_session_t));
   memset(loaded,0xa5,bytes);memset(pending,0x5a,bytes);br_pattern_session_loan_release();
   assert(br_session_save_blocked()&&!memcmp(saved,host_store,sizeof saved));
   assert(br_capture(&after)&&!memcmp(&before,&after,sizeof before));
   assert(!br_pattern_session_loan_acquire(4,test_exclusive,&exclusive,&loaded,&pending,&undo,&bytes));
   br_legacy_migration_pending=1;assert(br_pattern_session_scratch_blocked());
   assert(!br_pattern_session_loan_acquire(3,test_exclusive,&exclusive,&loaded,&pending,&undo,&bytes));
   br_legacy_migration_pending=0;br_save_pending=1;
   assert(!br_pattern_session_loan_acquire(3,test_exclusive,&exclusive,&loaded,&pending,&undo,&bytes));
   br_save_poll();assert(br_session_save_blocked()&&!memcmp(saved,host_store,sizeof saved));
 }
 puts("v16-only restore, protected settings and project scratch passed");return 0;}
