/* Pattern-only project round-trip, including all eleven independent snapshots. */
#include "pattern_project.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fixtures/project_v5_fixture.h"
static uint8_t source_arena[60000], target_arena[60000], work_arena[60000];
static uint8_t encoded[FM1_PROJECT_RECORD_MAX], wire[FM1_PROJECT_MAX], streamed[FM1_PROJECT_MAX];
static uint8_t session[FM1_PROJECT_SESSION], restored[FM1_PROJECT_SESSION], work_session[FM1_PROJECT_SESSION];
static br_pattern_snapshot live, clip, work_pattern, loaded, compare;
static br_pattern_bank bank, loaded_bank;
static fm1_pattern_song state, loaded_state;
static fm1_project_work work;
static fm1_project_source source;
static int valid(const uint8_t *p,size_t n,void *ctx) { (void)ctx; return n==FM1_PROJECT_SESSION && p[0]==0x42; }
static void fix_crc(uint8_t *w,size_t n){uint32_t c=~0u;for(size_t i=0;i<n;i++){c^=(i>=12&&i<16)?0:w[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}c=~c;w[12]=(uint8_t)c;w[13]=(uint8_t)(c>>8);w[14]=(uint8_t)(c>>16);w[15]=(uint8_t)(c>>24);}
static int valid_v5(const uint8_t *p,size_t n,void *ctx) { (void)ctx; return n==FM1_PROJECT_SESSION && p[0]==0x42; }
static int read_at(void *ctx,uint32_t off,void *dst,uint32_t n) { size_t len=*(size_t *)ctx; if(off>len || n>len-off) return -1; memcpy(dst,wire+off,n); return 0; }
static void init_pattern(br_pattern_snapshot *p) {
 memset(p,0,sizeof *p); for(unsigned i=0;i<BR_PARTS;i++){p->length[i]=64;p->division[i]=1;}
 memset(p->synth_note,255,sizeof p->synth_note); memset(p->synth_velocity,255,sizeof p->synth_velocity);
 memset(p->kit_note,255,sizeof p->kit_note); memset(p->kit_pitch,255,sizeof p->kit_pitch);
}
int main(void) {
 size_t len=0; session[0]=0x42; init_pattern(&live); fm1_pattern_song_init(&state);
 assert(br_pattern_bank_init(&bank,source_arena,sizeof source_arena)==BR_BANK_OK);
 for(unsigned i=1;i<FM1_PATTERN_COUNT;i++) { clip=live; clip.step[0][i]=i+1; clip.synth_note[0][i]=(uint8_t)(48+i); assert(br_pattern_bank_paste(&bank,i,&live,&clip,encoded,sizeof encoded)==BR_BANK_OK); }
 state.used=bank.used; state.active=bank.active; assert(state.used==0x7ff);
 /* An arrangement with loops, SONG mode, LOOP end: saved with the project. */
 for(unsigned i=0;i<64;i++) assert(fm1_arr_insert(&state,(uint8_t)i,(uint8_t)((i*7)%11),(uint8_t)(i%16+1))==FM1_ARR_OK);
 assert(fm1_arr_loop_set(&state,3,5,4)==FM1_ARR_OK && fm1_arr_loop_set(&state,63,63,16)==FM1_ARR_OK);
 assert(fm1_arr_set_mode(&state,FM1_SONG_MODE_SONG)==FM1_ARR_OK && fm1_arr_set_end(&state,FM1_SONG_END_LOOP)==FM1_ARR_OK);
 work.compressed=encoded; work.compressed_capacity=sizeof encoded; work.session=work_session; work.arena=work_arena; work.arena_capacity=sizeof work_arena; work.pattern=&work_pattern;
 assert(fm1_project_encode(session,&bank,&live,&state,valid,0,&work,wire,sizeof wire,&len)==FM1_PROJECT_OK);
 assert(len>FM1_PROJECT_SESSION+FM1_PROJECT_HEADER_BYTES && wire[4]==6 && wire[5]==0 && wire[6]==FM1_PROJECT_HEADER_BYTES && !wire[19] && wire[20]==(uint8_t)FM1_PROJECT_SESSION && wire[21]==(uint8_t)(FM1_PROJECT_SESSION>>8) && !wire[22] && !wire[23]);
 assert(fm1_project_source_init(&source,session,&bank,&live,&state,valid,0,&work)==FM1_PROJECT_OK && source.total==len);
 for(uint32_t i=0;i<len;) { uint32_t n=(len-i>117)?117:(uint32_t)(len-i); assert(!fm1_project_source_read(&source,i,streamed+i,n)); i+=n; }
 assert(!memcmp(wire,streamed,len));
 assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_OK);
 assert(!memcmp(session,restored,sizeof session) && loaded_bank.used==0x7ff && loaded_bank.active==0);
 assert(loaded_state.song_length==64 && loaded_state.loop_count==2 && loaded_state.mode==FM1_SONG_MODE_SONG && loaded_state.end==FM1_SONG_END_LOOP);
 assert(!memcmp(loaded_state.entry,state.entry,sizeof state.entry) && !memcmp(loaded_state.loop,state.loop,sizeof state.loop) && loaded_state.used==0x7ff);
 { static uint8_t mem_bank_arena[60000]; br_pattern_bank mb; br_pattern_snapshot ml; fm1_pattern_song ms; memset(&ms,0,sizeof ms);
   assert(fm1_project_decode(wire,len,valid,0,&work,restored,&mb,mem_bank_arena,sizeof mem_bank_arena,&ml,&ms)==FM1_PROJECT_OK);
   assert(!memcmp(ms.entry,state.entry,sizeof state.entry) && !memcmp(ms.loop,state.loop,sizeof state.loop) && ms.mode==1 && ms.end==1); }
 /* Save -> clear -> load: a cleared arrangement saves empty, and loading the first project restores it. */
 { static uint8_t wire2[FM1_PROJECT_MAX]; size_t len2=0; fm1_pattern_song cleared=state; assert(fm1_arr_clear(&cleared)==FM1_ARR_OK); cleared.mode=0; cleared.end=0;
   assert(fm1_project_encode(session,&bank,&live,&cleared,valid,0,&work,wire2,sizeof wire2,&len2)==FM1_PROJECT_OK);
   for(unsigned i=0;i<FM1_PROJECT_ARRANGEMENT;i++) assert(!wire2[FM1_PROJECT_HEADER_BYTES+FM1_PROJECT_SESSION+i]);
   assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_OK);
   assert(loaded_state.song_length==64 && loaded_state.mode==1); }
 /* A damaged arrangement (CRC fixed up) is refused, never misread. */
 { static uint8_t keep[FM1_PROJECT_MAX]; memcpy(keep,wire,len); wire[FM1_PROJECT_HEADER_BYTES+FM1_PROJECT_SESSION+16]=0x0b;
   { uint32_t c=~0u; for(size_t i=0;i<len;i++){c^=(i>=12&&i<16)?0:wire[i];for(unsigned j=0;j<8;j++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));} c=~c; wire[12]=(uint8_t)c;wire[13]=(uint8_t)(c>>8);wire[14]=(uint8_t)(c>>16);wire[15]=(uint8_t)(c>>24); }
   assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID);
   memcpy(wire,keep,len); }
 for(unsigned i=0;i<FM1_PATTERN_COUNT;i++){ assert(br_pattern_bank_copy(&loaded_bank,i,&loaded,&compare)==BR_BANK_OK); if(i==0) assert(!memcmp(&compare,&live,sizeof live)); else { clip=live; clip.step[0][i]=i+1; clip.synth_note[0][i]=(uint8_t)(48+i); assert(!memcmp(&compare,&clip,sizeof clip)); } }
 fm1_project_old_format_seen=0u; wire[4]=4; assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID && fm1_project_old_format_seen); /* envelope 4 = old project */
 wire[4]=7; fm1_project_old_format_seen=0u; assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID && !fm1_project_old_format_seen); wire[4]=6;
 wire[len-1]^=1; assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID);
 /* A real version-5 project (written by b9ef12f, tests/fixtures/gen_v5_project.c.txt) loads exactly as
  * b9ef12f loads it, with an empty arrangement; saved again it is version 6. */
 { static uint8_t v5[sizeof v5_image]; static uint8_t arena2[60000]; size_t n5=sizeof v5_image; static fm1_pattern_song st;
   int pass;
   memcpy(v5,v5_image,n5); assert(v5[4]==5);
   static br_pattern_bank kb; static br_pattern_snapshot kl; static fm1_pattern_song ks; static uint8_t ksess[FM1_PROJECT_SESSION];
   for(pass=0;pass<3;pass++){
     if(pass==2){ kb=loaded_bank; kl=loaded; ks=st; memcpy(ksess,restored,sizeof ksess); }
     memset(restored,0,sizeof restored); memset(&loaded,0,sizeof loaded); memset(&loaded_bank,0,sizeof loaded_bank);
     memset(&st,0x5a,sizeof st); fm1_project_old_format_seen=0u;
     if(pass==0){ memcpy(wire,v5,n5); size_t l=n5; assert(fm1_project_decode_stream(read_at,&l,(uint32_t)l,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_OK); }
     else if(pass==1) assert(fm1_project_decode(v5,n5,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_OK);
     else { size_t l2=0; static uint8_t again[FM1_PROJECT_MAX];
       assert(fm1_project_encode(ksess,&kb,&kl,&ks,valid_v5,0,&work,again,sizeof again,&l2)==FM1_PROJECT_OK);
       assert(again[4]==6 && l2==n5+FM1_PROJECT_ARRANGEMENT);
       memset(&loaded,0,sizeof loaded); memset(restored,0,sizeof restored);
       assert(fm1_project_decode(again,l2,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_OK); }
     assert(!fm1_project_old_format_seen);
     assert(!memcmp(restored,v5_session,sizeof v5_session));
     assert(loaded_bank.used==V5_USED && loaded_bank.active==V5_ACTIVE && loaded_bank.size==sizeof v5_arena);
     for(unsigned i=0;i<11;i++) assert(loaded_bank.offset[i]==v5_offset[i] && loaded_bank.length[i]==v5_length[i]);
     assert(!memcmp(arena2,v5_arena,sizeof v5_arena) && !memcmp(&loaded,v5_live,sizeof v5_live));
     assert(st.used==V5_USED && st.active==V5_ACTIVE && st.queued==FM1_PATTERN_NONE);
     assert(!st.song_length && !st.loop_count && st.mode==FM1_SONG_MODE_PATTERN && st.end==FM1_SONG_END_STOP && !st.song_enabled);
   }
   /* Version 4 and older stay OLD FORMAT; a damaged v5 is refused, not old. */
   memcpy(v5,v5_image,n5); v5[4]=4; fix_crc(v5,n5); fm1_project_old_format_seen=0u;
   assert(fm1_project_decode(v5,n5,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_INVALID && fm1_project_old_format_seen);
   memcpy(v5,v5_image,n5); v5[n5-1]^=1; fm1_project_old_format_seen=0u;
   assert(fm1_project_decode(v5,n5,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_INVALID && !fm1_project_old_format_seen);
   assert(fm1_project_decode(v5_image,FM1_PROJECT_HEADER_BYTES+100u,valid_v5,0,&work,restored,&loaded_bank,arena2,sizeof arena2,&loaded,&st)==FM1_PROJECT_INVALID);
 }
 puts("pattern-only 11-pattern project and arrangement round-trip passed; real v5 project loads as b9ef12f did"); return 0;
}
