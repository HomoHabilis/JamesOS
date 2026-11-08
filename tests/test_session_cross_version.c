/* Host-only: exercise the actual 1681 and candidate diagnostic session path. */
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "../firmware/src/instrument_fx_progress.h"
uint32_t br_fx_init_test_calls, br_app_test_inject_fault_before_mutation;
#define FM1_TICKS_PER_US 24u
static struct { uint32_t last_ticks; } fm1_audio_metrics;
static uint32_t fm1_ticks(void) { static uint32_t tick; return ++tick; }
static void fm1_audio_metrics_snapshot(uint32_t out[8]) { memset(out,0,32u); }
static void fm1_audio_metrics_reset(void) {}
static void fm1_uac_diagnostic_snapshot(uint32_t out[19]) { memset(out,0,76u); out[0]=1u; }
#define DINGHY_HOST 1
#define BR_SPLIT_AUDIO 1
#define BR_DIAGNOSTIC_TEST 1
#define main unused_host_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/instrument_audio_unit.c"
#ifdef CROSS_PATTERN
#undef br_ps_select
#undef br_ps_copy_pattern
#undef br_ps_paste_pattern
#undef br_ps_set_entry
#undef br_ps_set_length
#undef br_ps_enable_song
#include "../firmware/src/pattern_snapshot.c"
#include "../firmware/src/pattern_owner.c"
static uint8_t pattern_arena[16384], pattern_encoded[9000];
static br_pattern_snapshot pattern_live, pattern_work, pattern_clip, pattern_expected, pattern_result;
static int pattern_quiet(void *unused) { (void)unused; return br_diagnostic_restore_in_progress && !br_playing; }
static void make_stale_bank(void) {
 assert(br_quiescent_transaction_begin());
 assert(br_ps_host_init(pattern_arena,sizeof pattern_arena,&pattern_live,&pattern_work,&pattern_clip,
                        pattern_encoded,sizeof pattern_encoded,pattern_quiet,0));
 assert(br_ps_select(1u));
 assert(br_ps_copy_pattern(1u));
 assert(fm1_song_entry_set(&br_ps_state,0u,1u,4u));
 assert(fm1_song_length_set(&br_ps_state,2u));
 assert(br_ps_state.used==3u && br_ps_state.active==1u);
 br_quiescent_transaction_end();
}
static void assert_restored_pattern(void) {
 br_pattern_bank view; fm1_pattern_song song, initial;
 assert(br_quiescent_transaction_begin());
 assert(!br_pattern_snapshot_capture(&pattern_expected));
 assert(br_ps_host_export(&view,&pattern_result,&song));
 fm1_pattern_song_init(&initial);
 assert(view.used==1u && view.active==0u && view.size==0u && view.clip_length==0u);
 assert(!memcmp(&br_ps_state,&initial,sizeof initial));
 assert(!memcmp(&song,&initial,sizeof song));
 assert(!memcmp(&pattern_result,&pattern_expected,sizeof pattern_result));
 assert(!memcmp(&pattern_live,&pattern_expected,sizeof pattern_live));
 assert(!br_ps_paste_pattern(2u));
 assert(br_ps_select(1u));
 assert(!br_pattern_snapshot_capture(&pattern_result));
 assert(!memcmp(&pattern_result,&pattern_expected,sizeof pattern_result));
 assert(br_ps_select(0u));
 assert(!br_pattern_snapshot_capture(&pattern_result));
 assert(!memcmp(&pattern_result,&pattern_expected,sizeof pattern_result));
 br_quiescent_transaction_end();
}
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static void export_session(uint8_t *out) {
 uint8_t reply[128], body[3]; uint32_t n;
 for(uint32_t off=0;off<sizeof(br_session_t);off+=32u) {
  uint32_t count=sizeof(br_session_t)-off; if(count>32u)count=32u;
  body[0]=(uint8_t)off; body[1]=(uint8_t)(off>>8); body[2]=(uint8_t)count;
  assert(fm1_app_command(0x63u,body,3u,reply,&n) && n==count);
  memcpy(out+off,reply,count);
 }
}
static void upload(const uint8_t *data,int incomplete) {
 uint8_t reply[128], body[34], commit[4]={'B','R',1u,0u}; uint32_t n;
 for(uint32_t off=0;off<sizeof(br_session_t);off+=32u) {
  uint32_t count=sizeof(br_session_t)-off; if(count>32u)count=32u;
  if(incomplete && off==32u)continue;
  body[0]=(uint8_t)off;body[1]=(uint8_t)(off>>8);memcpy(body+2,data+off,count);
  assert(fm1_app_command(0x64u,body,count+2u,reply,&n) && n==2u);
 }
 int ok=fm1_app_command(0x65u,commit,4u,reply,&n);
 assert(incomplete ? !ok : (ok && n==1u && reply[0]==1u));
}
static void fill(unsigned seed) {
 br_bpm=80u+seed; br_ui.part=seed%BR_PARTS; br_ui.octave=(int8_t)(seed%5u)-2;
 for(uint32_t p=0;p<BR_PARTS;p++) {
  br_midi_channels[p]=(uint8_t)(1u+(seed+p*3u)%16u);
  br_seq_record_quantize[p]=(uint8_t)((seed+p)%BR_TIME_QUANT_COUNT);
  br_part[p].sound.pan=(seed*17u+p*23u)%128u;
  br_part[p].length=(uint8_t)(1u+(seed+p*7u)%BR_STEPS);
 }
 for(uint32_t i=4;i<7;i++)br_midi_channels[i]=(uint8_t)(1u+(seed+i*3u)%16u);
 br_midi_clock_enabled=seed&1u;
 for(uint32_t p=0;p<BR_SYNTH_PARTS;p++)for(uint32_t s=0;s<BR_STEPS;s++) {
  br_seq_synth_note[p][s]=(uint8_t)((seed+p*19u+s)%128u);
  br_part[p].step[s]=BR_NOTE_STEP((seed+p*19u+s)%128u,100u);
  br_seq_synth_velocity[p][s]=(uint8_t)((seed+s*7u)%128u);
  br_seq_synth_length[p][s]=(uint16_t)(1u+(seed+s*13u)%200u);
  br_seq_synth_micro[p][s]=(int8_t)((s+seed)%17u)-8;
 }
 for(uint32_t s=0;s<BR_STEPS;s++)for(uint32_t l=0;l<BR_DRUM_LANES;l++) {
  br_part[BR_DRUM_PART].step[s]=(s+seed)%2u ? 15u : 5u;
  br_seq_kit_length[s][l]=(uint16_t)(1u+(seed+s*3u+l)%200u);
  br_seq_kit_micro[s][l]=(int8_t)((seed+s+l)%17u)-8;
  br_seq_kit_velocity[s][l]=(uint8_t)((seed+s+l*5u)%128u);
  br_seq_kit_retrig[s][l]=(uint8_t)((seed+s+l)%64u);
 }
}
/* The saved session provides track modes; synthetic measured FX costs drive
 * the real adaptive split logic. Host times are not device timings. */
static void replay_graph(void) {
 static int32_t graph_bus[BR_PARTS][BR_BLOCK];
 static const uint32_t costs[][3]={{1000,1000,1000},{3001,2500,3500},{2500,3001,2500},{1999,1999,1999}};
 unsigned i,p,j,n,rv;
 br_audio_source_fx_fusion_active=1;
 for(rv=0;rv<2;rv++){
  memset(br_audio_split_state,0,sizeof br_audio_split_state);
  br_gr_run=br_gr_key_valid=rv;
  for(i=0;i<sizeof(costs)/sizeof(costs[0]);i++){
   for(p=0;p<3;p++)br_fx_job_cost[p]=costs[i][p];
   n=br_audio_build_fusion_jobs(graph_bus,BR_BLOCK);
   printf("graph reverb %u step %u count %u jobs",rv,i,n);
   for(j=0;j<n;j++)printf(" %u:%u",br_audio_source_jobs[j].kind,br_audio_source_jobs[j].part);
   puts("");
  }
 }
}
int main(int argc,char **argv) {
 assert(argc==3 && sizeof(br_session_t)==3484u); br_app_init();
 uint8_t data[sizeof(br_session_t)], out[sizeof(br_session_t)]; FILE *f;
 if(!strcmp(argv[1],"create")) {
  fill(37u); export_session(data); assert(data[7]==16u);
  f=fopen(argv[2],"wb"); assert(f && fwrite(data,1,sizeof data,f)==sizeof data && !fclose(f));
 } else {
  assert(!strcmp(argv[1],"verify"));f=fopen(argv[2],"rb");
  assert(f && fread(data,1,sizeof data,f)==sizeof data && fgetc(f)==EOF && !fclose(f));
#ifdef CROSS_PATTERN
  make_stale_bank();
  /* V16-only still accepts legacy upload framing but rejects commit, and
   * reports no legacy migration record; neither operation mutates live data. */
  {
   uint8_t old[sizeof(br_session_t)], before_old[sizeof(br_session_t)],after_old[sizeof(br_session_t)];
   uint8_t reply[128], body[34],commit[4]={'B','R',1u,0u};uint32_t n;
   export_session(before_old);
   for(uint32_t v=10u;v<=15u;v++) {
    size_t length=v==10u?sizeof(br_session_v10_t):sizeof(br_session_t);
    memcpy(old,data,sizeof old);old[7]=(uint8_t)v;
    for(size_t off=0;off<length;off+=32u) {
     size_t count=length-off<32u?length-off:32u;
     body[0]=(uint8_t)off;body[1]=(uint8_t)(off>>8);memcpy(body+2,old+off,count);
     assert(fm1_app_command(0x64u,body,(uint32_t)count+2u,reply,&n));
    }
    assert(!fm1_app_command(0x65u,commit,4u,reply,&n));
    export_session(after_old);assert(!memcmp(before_old,after_old,sizeof before_old));
   }
   assert(fm1_app_command(0x68u,0,0u,reply,&n) && n==12u && reply[3]==0u);
   body[0]=body[1]=0u;body[2]=32u;assert(!fm1_app_command(0x68u,body,3u,reply,&n));
  }
#endif
  uint8_t before[sizeof(br_session_t)]; export_session(before);
  upload(data,1); export_session(out);
  assert(!memcmp(before,out,sizeof before));
#ifdef CROSS_PATTERN
  assert(br_ps_state.used==3u && br_ps_state.active==1u && br_ps_state.entry[0].pattern==1u);
#endif
  upload(data,0); printf("restored audio modes %u %u %u %u\n",br_mode[0],br_mode[1],br_mode[2],br_mode[3]); export_session(out);
  assert(!memcmp(data,out,sizeof data));
#ifdef CROSS_PATTERN
  assert_restored_pattern();
#endif
  replay_graph();
 }
 puts("PASS: diagnostic v16 session"); return 0;
}
