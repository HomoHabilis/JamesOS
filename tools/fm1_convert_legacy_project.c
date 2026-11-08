/* SPDX-License-Identifier: GPL-3.0-only */
/* Read-only host conversion of one legacy session to a single-pattern v4 payload. */
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
#include <stdio.h>
#include <string.h>
static br_session_t input, current, scratch_session;
static uint8_t arena[7680], decoded_arena[7680], work_arena[7680], session_scratch[3484], decoded_session[3484], wire[3584];
static br_pattern_snapshot original, restored, work_pattern, decoded;
static br_pattern_bank bank, decoded_bank;
static fm1_pattern_song song, decoded_song;
static fm1_project_source source;
static fm1_project_work work;
static int valid(const uint8_t *p,size_t n,void *ctx){return br_pattern_session_validate(p,n,ctx);}
static int write_new(const char *path,const void *data,size_t n){FILE *f=fopen(path,"wbx");if(!f){perror(path);return 0;} if(fwrite(data,1,n,f)!=n){fclose(f);return 0;}return !fclose(f);}
int main(int argc,char **argv){
 size_t n=0; FILE *f; int rc;unsigned version;
 if(argc!=4){fprintf(stderr,"usage: convert INPUT-SESSION OUTPUT-SESSION OUTPUT-PAYLOAD\n");return 2;}
 f=fopen(argv[1],"rb");if(!f || fread(&input,1,sizeof input,f)!=sizeof input || fgetc(f)!=EOF || fclose(f)){fprintf(stderr,"invalid input length\n");return 2;}
 version=input.v3.version;br_app_init();if(!br_pattern_session_bind(&scratch_session,sizeof scratch_session)||!br_restore(&input)||br_pattern_snapshot_capture(&original)||!br_capture(&current)||!br_session_v16_valid(&current)){fprintf(stderr,"session validation/conversion failed\n");return 3;}
 br_app_init();if(!br_restore(&current)||br_pattern_snapshot_capture(&restored)||memcmp(&original,&restored,sizeof original)){fprintf(stderr,"pattern changed after conversion\n");return 3;}
 if(br_pattern_bank_init(&bank,arena,sizeof arena)!=BR_BANK_OK){fprintf(stderr,"bank initialization failed\n");return 3;}
 fm1_pattern_song_init(&song);memset(&work,0,sizeof work);work.pattern=&work_pattern;work.session=session_scratch;work.arena=work_arena;work.arena_capacity=sizeof work_arena;
 rc=fm1_project_source_init(&source,(const uint8_t *)&current,&bank,&original,&song,valid,0,&work);
 if(rc!=FM1_PROJECT_OK){fprintf(stderr,"project initialization failed (%d)\n",rc);return 3;}
 rc=fm1_codec_source_encode(fm1_project_source_read,&source,source.total,wire,sizeof wire,&n);
 if(rc!=FM1_CODEC_OK){fprintf(stderr,"project encoding failed (%d, required %zu bytes)\n",rc,n);return 4;}
 rc=fm1_compressed_project_decode(wire,n,valid,0,&work,decoded_session,&decoded_bank,decoded_arena,sizeof decoded_arena,&decoded,&decoded_song);
 if(rc!=FM1_PROJECT_OK||memcmp(decoded_session,&current,sizeof current)||memcmp(&decoded,&original,sizeof original)||decoded_bank.used!=1u||decoded_bank.active!=0u){fprintf(stderr,"round-trip verification failed (%d)\n",rc);return 3;}
 if(!write_new(argv[2],&current,sizeof current))return 5;
 if(!write_new(argv[3],wire,n))return 5;
 printf("source version %u -> v16, one pattern verified, payload %zu bytes\n",version,n);return 0;
}
