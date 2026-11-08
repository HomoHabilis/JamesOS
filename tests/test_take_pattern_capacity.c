/* Host-only: actual session, pattern-bank and fixed-project codec capacity. */
#define DINGHY_HOST 1
#define BR_PATTERN_BANK_EXPERIMENT 1
#define BR_PATTERN_SNAPSHOT_MONOLITHIC_TEST 1
#define main host_unused_main
#include "../firmware/src/dinghy.c"
#undef main
#include "../firmware/src/pattern_snapshot.c"
#include "pattern_project.h"
#include "pattern_codec_source.h"
#include "pattern_storage_codec.h"
#include "pattern_compressed_project.h"
#include <assert.h>
#include <stdio.h>
static uint8_t session[3484], small_arena[7680], large_arena[60000], encoded[4335], wire[3580], temp_session[3484];
static br_session_t session_scratch;
static br_pattern_snapshot initial, small_live, large_live, small_work, large_work, project_pattern, compare;
static br_pattern_bank small,large;
static fm1_project_work project_work;
static fm1_project_source source;
static fm1_pattern_song song;
static uint8_t arena_backup[7680];
static br_pattern_snapshot live_backup, expected_target, changed_outgoing;
static uint8_t load_arena[3584], loaded_session[3484];
static br_pattern_bank loaded_bank;
static br_pattern_snapshot loaded_live;
static fm1_pattern_song loaded_song;
static int valid(const uint8_t *p,size_t n,void *ctx){return br_pattern_session_validate(p,n,ctx);}
static uint32_t rng=0x91ad42u;
static uint32_t rnd(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void dense(br_pattern_snapshot *p) {
 for(unsigned t=0;t<4;t++){p->length[t]=64;p->division[t]=1;for(unsigned i=0;i<64;i++)p->step[t][i]=t==3 ? (rnd()&255u) : BR_NOTE_STEP(rnd()%128u,1u+rnd()%127u);}
 for(unsigned t=0;t<3;t++)for(unsigned i=0;i<64;i++){
  p->synth_note[t][i]=rnd()%128;p->synth_velocity[t][i]=rnd()%128;p->synth_length[t][i]=1u+rnd()%500;
  p->synth_micro[t][i]=(int)(rnd()%32)-16;p->synth_condition[t][i]=rnd()%BR_STEP_COND_COUNT; }
 for(unsigned i=0;i<64;i++)for(unsigned l=0;l<4;l++){
  p->kit_note[i][l]=255;p->kit_pitch[i][l]=36u+rnd()%49;p->kit_velocity[i][l]=rnd()%128;
  p->kit_length[i][l]=1u+rnd()%500;p->kit_micro[i][l]=(int)(rnd()%32)-16;
  p->kit_condition[i][l]=rnd()%BR_STEP_COND_COUNT;p->kit_retrig[i][l]=rnd()%64; }
}
static void measure(const char *label,int scenario){
 int small_full=0; size_t need; int r;
 assert(!br_pattern_bank_init(&small,small_arena,sizeof small_arena));
 assert(!br_pattern_bank_init(&large,large_arena,sizeof large_arena));
 small_live=large_live=initial; if(scenario) { rng=0x91ad42u; dense(&small_live); large_live=small_live; }
 memset(&project_work,0,sizeof project_work); project_work.pattern=&project_pattern;project_work.session=temp_session;
 fm1_pattern_song_init(&song);
 for(unsigned n=1;n<=11;n++){
  song.used=large.used;song.active=large.active;
  assert(fm1_project_source_init(&source,session,&large,&large_live,&song,valid,0,&project_work)==FM1_PROJECT_OK);
  memset(wire,0xa5,sizeof wire);need=0;
  r=fm1_codec_source_encode(fm1_project_source_read,&source,source.total,wire,sizeof wire,&need);
  assert(r==FM1_CODEC_OK||r==FM1_CODEC_FULL);
  if(r==FM1_CODEC_FULL)for(unsigned j=0;j<sizeof wire;j++)assert(wire[j]==0xa5);
  printf("%s %s count=%u reference_bank=%zu source=%u compressed=%zu flash=%s small_bank=%zu small_full=%d",label,scenario==0?"clone":scenario==1?"dense-identical":"dense-varied",n,large.size,source.total,need,r==FM1_CODEC_OK?"fits":"FULL",small.size,small_full);
  if(!small_full){
   unsigned target=n<11?n:0u;
   int q=br_pattern_bank_prepare(&small,target,&small_live,&small_work,sizeof encoded);
   printf(" running_prepare=%s",q==BR_BANK_OK?"fits":q==BR_BANK_FULL?"FULL":"INVALID");
   if(q==BR_BANK_OK)assert(br_pattern_bank_cancel(&small)==BR_BANK_OK);
  }
  puts("");
  if(n==11 && scenario==0 && !small_full){
   assert(r==FM1_CODEC_OK);
   project_work.arena=load_arena;project_work.arena_capacity=1;
   assert(fm1_compressed_project_decode(wire,need,valid,0,&project_work,loaded_session,&loaded_bank,load_arena,1,&loaded_live,&loaded_song)==FM1_PROJECT_FULL);
   project_work.arena_capacity=sizeof load_arena;
   int decode=fm1_compressed_project_decode(wire,need,valid,0,&project_work,loaded_session,&loaded_bank,load_arena,sizeof load_arena,&loaded_live,&loaded_song);
   if(large.size<=sizeof load_arena){
    assert(decode==FM1_PROJECT_OK);
    assert(!memcmp(loaded_session,session,sizeof session) && !memcmp(&loaded_live,&large_live,sizeof loaded_live));
    assert(loaded_bank.used==large.used && loaded_bank.active==large.active && loaded_bank.size==large.size);
    for(unsigned j=0;j<11;j++){assert(br_pattern_bank_copy(&loaded_bank,j,&loaded_live,&compare)==BR_BANK_OK);assert(!memcmp(&compare,&initial,sizeof compare));}
   }else assert(decode==FM1_PROJECT_FULL);
   puts("PASS: compressed project rejects insufficient staging and uses bounded staging safely");
   project_work.arena=0;project_work.arena_capacity=0;
   assert(br_pattern_bank_clip_copy(&small,10u,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
   assert(small.clip_length>0u && small.size==11u*small.clip_length);
   br_pattern_bank old=small; live_backup=small_live; memcpy(arena_backup,small_arena,sizeof small_arena);
   for(unsigned target=0;target<10;target++){
    assert(br_pattern_bank_copy(&small,target,&small_live,&expected_target)==BR_BANK_OK);
    assert(br_pattern_bank_prepare(&small,target,&small_live,&small_work,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&small_work,&expected_target,sizeof small_work));
    assert(br_pattern_bank_cancel(&small)==BR_BANK_OK);
    { br_pattern_bank normalized=small;normalized.reserved_target=old.reserved_target;
      assert(!memcmp(&normalized,&old,sizeof old)&&!memcmp(small_arena,arena_backup,sizeof arena_backup)); }
    assert(br_pattern_bank_prepare(&small,target,&small_live,&small_work,sizeof encoded)==BR_BANK_OK);
    rng=0x882201u+target;for(size_t i=0;i<sizeof small_live;i++)((uint8_t *)&small_live)[i]=(uint8_t)rnd();
    changed_outgoing=small_live;
    assert(br_pattern_bank_commit(&small,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&small_live,&expected_target,sizeof small_live));
    assert(br_pattern_bank_copy(&small,10u,&small_live,&compare)==BR_BANK_OK && !memcmp(&compare,&changed_outgoing,sizeof compare));
    for(unsigned j=0;j<10;j++)if(j!=target){assert(br_pattern_bank_copy(&small,j,&small_live,&compare)==BR_BANK_OK);assert(!memcmp(&compare,&initial,sizeof compare));}
    assert(br_pattern_bank_verify(&small)==BR_BANK_OK);
    assert(br_pattern_bank_clip_paste(&small,target,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&small_live,&initial,sizeof small_live));
    small=old;small_live=live_backup;memcpy(small_arena,arena_backup,sizeof small_arena);
   }
   puts("PASS: all ten existing targets reserve, cancel and commit with worst-case outgoing edits");
   /* Mirror the user's copy/paste workflow: clipboard remains present while
    * ten additional slots are created and while the queued switch commits. */
   assert(br_pattern_bank_init(&small,small_arena,sizeof small_arena)==BR_BANK_OK);
   small_live=initial;
   assert(br_pattern_bank_clip_copy(&small,0,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
   for(unsigned j=1;j<11;j++)assert(br_pattern_bank_clip_paste(&small,j,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
   assert(small.used==0x7ffu && small.active==0u && small.clip_length>0u && small.size==11u*small.clip_length);
   old=small; live_backup=small_live;memcpy(arena_backup,small_arena,sizeof small_arena);
   for(unsigned target=1;target<11;target++){
    assert(br_pattern_bank_prepare(&small,target,&small_live,&small_work,sizeof encoded)==BR_BANK_OK);
    assert(br_pattern_bank_cancel(&small)==BR_BANK_OK);
    {br_pattern_bank x=small;x.reserved_target=old.reserved_target;assert(!memcmp(&x,&old,sizeof old)&&!memcmp(small_arena,arena_backup,sizeof arena_backup));}
    assert(br_pattern_bank_prepare(&small,target,&small_live,&small_work,sizeof encoded)==BR_BANK_OK);
    rng=0x123457u+target;for(size_t i=0;i<sizeof small_live;i++)((uint8_t *)&small_live)[i]=(uint8_t)rnd();
    changed_outgoing=small_live;
    {br_pattern_bank reserved=small;
     assert(br_pattern_bank_commit(&small,&small_live,&small_work,encoded,1u)==BR_BANK_FULL);
     assert(!memcmp(&small,&reserved,sizeof reserved) && !memcmp(&small_live,&changed_outgoing,sizeof small_live) && !memcmp(small_arena,arena_backup,sizeof arena_backup));}
    assert(br_pattern_bank_commit(&small,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&small_live,&initial,sizeof initial));
    assert(br_pattern_bank_copy(&small,0,&small_live,&compare)==BR_BANK_OK && !memcmp(&compare,&changed_outgoing,sizeof compare));
    assert(br_pattern_bank_clip_paste(&small,target,&small_live,&small_work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&small_live,&initial,sizeof initial));
    small=old;small_live=live_backup;memcpy(small_arena,arena_backup,sizeof small_arena);
   }
   puts("PASS: copy/paste-created 11 patterns retain clipboard through queued cancel and worst-case commit");
  }
  if(n==11)break;
  if(scenario==2){dense(&large_live);if(!small_full)small_live=large_live;}
  assert(br_pattern_bank_select(&large,n,&large_live,&large_work,encoded,sizeof encoded)==BR_BANK_OK);
  if(!small_full){
   br_pattern_bank old=small; br_pattern_snapshot oldlive=small_live;uint8_t backup[sizeof small_arena];memcpy(backup,small_arena,sizeof backup);
   int result=br_pattern_bank_select(&small,n,&small_live,&small_work,encoded,sizeof encoded);
   if(result==BR_BANK_FULL){small_full=1;assert(!memcmp(&small,&old,sizeof old)&&!memcmp(&small_live,&oldlive,sizeof oldlive)&&!memcmp(backup,small_arena,sizeof backup));}
   else assert(result==BR_BANK_OK);
  }
 }
 assert(br_pattern_bank_verify(&large)==BR_BANK_OK);
 for(unsigned i=0;i<11;i++)assert(br_pattern_bank_copy(&large,i,&large_live,&compare)==BR_BANK_OK);
}
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f&&fread(session,1,sizeof session,f)==sizeof session&&fgetc(f)==EOF);fclose(f);
 br_app_init();assert(br_pattern_session_bind(&session_scratch,sizeof session_scratch));
 assert(br_pattern_session_snapshot(session,sizeof session,&initial));assert(br_restore((const br_session_t *)session));
 assert(!br_pattern_snapshot_capture(&compare));assert(!memcmp(&initial,&compare,sizeof initial));
 measure(argv[1],0);measure(argv[1],1);measure(argv[1],2);return 0;
}
