/* Pattern-only project round-trip, including all eleven independent snapshots. */
#include "pattern_project.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t source_arena[60000], target_arena[60000], work_arena[60000];
static uint8_t encoded[FM1_PROJECT_RECORD_MAX], wire[FM1_PROJECT_MAX], streamed[FM1_PROJECT_MAX];
static uint8_t session[FM1_PROJECT_SESSION], restored[FM1_PROJECT_SESSION], work_session[FM1_PROJECT_SESSION];
static br_pattern_snapshot live, clip, work_pattern, loaded, compare;
static br_pattern_bank bank, loaded_bank;
static fm1_pattern_song state, loaded_state;
static fm1_project_work work;
static fm1_project_source source;
static int valid(const uint8_t *p,size_t n,void *ctx) { (void)ctx; return n==FM1_PROJECT_SESSION && p[0]==0x42; }
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
 work.compressed=encoded; work.compressed_capacity=sizeof encoded; work.session=work_session; work.arena=work_arena; work.arena_capacity=sizeof work_arena; work.pattern=&work_pattern;
 assert(fm1_project_encode(session,&bank,&live,&state,valid,0,&work,wire,sizeof wire,&len)==FM1_PROJECT_OK);
 assert(len>FM1_PROJECT_SESSION+FM1_PROJECT_HEADER_BYTES && wire[4]==4 && wire[5]==0 && wire[6]==FM1_PROJECT_HEADER_BYTES && !wire[19]);
 assert(fm1_project_source_init(&source,session,&bank,&live,&state,valid,0,&work)==FM1_PROJECT_OK && source.total==len);
 for(uint32_t i=0;i<len;) { uint32_t n=(len-i>117)?117:(uint32_t)(len-i); assert(!fm1_project_source_read(&source,i,streamed+i,n)); i+=n; }
 assert(!memcmp(wire,streamed,len));
 assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_OK);
 assert(!memcmp(session,restored,sizeof session) && loaded_bank.used==0x7ff && loaded_bank.active==0);
 for(unsigned i=0;i<FM1_PATTERN_COUNT;i++){ assert(br_pattern_bank_copy(&loaded_bank,i,&loaded,&compare)==BR_BANK_OK); if(i==0) assert(!memcmp(&compare,&live,sizeof live)); else { clip=live; clip.step[0][i]=i+1; clip.synth_note[0][i]=(uint8_t)(48+i); assert(!memcmp(&compare,&clip,sizeof clip)); } }
 wire[4]=1; assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID); wire[4]=4;
 wire[len-1]^=1; assert(fm1_project_decode_stream(read_at,&len,(uint32_t)len,valid,0,&work,restored,&loaded_bank,target_arena,sizeof target_arena,&loaded,&loaded_state)==FM1_PROJECT_INVALID);
 puts("pattern-only 11-pattern project round-trip passed"); return 0;
}
