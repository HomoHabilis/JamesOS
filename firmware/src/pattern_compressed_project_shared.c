/* Host-only candidate for replacing duplicate PSC3 replay with stream decoder. */
#if (!defined(FM1_SHARED_PSC_HOST_TEST) || !FM1_SHARED_PSC_HOST_TEST || defined(FM1_DEVICE)) && (!defined(FM1_PATTERN_DEVICE_CANDIDATE) || !FM1_PATTERN_DEVICE_CANDIDATE)
#error "Shared PSC candidate is host-only and target-unqualified"
#endif
#include "pattern_compressed_project.h"
#include "pattern_overlap.h"
#include "pattern_stream_project.h"
#include <stdint.h>
#include <string.h>
typedef struct {const uint8_t *p;uint32_t len;} memory_source;
static int read_memory(void *v,uint32_t off,void *out,uint32_t n){memory_source *s=v;if(!s||(!out&&n)||off>s->len||n>s->len-off||fm1_mem_overlap(out,n,s->p,s->len))return -1;if(n)memcpy(out,s->p+off,n);return 0;}
int fm1_compressed_project_decode(const uint8_t *data,size_t length,fm1_project_session_valid valid,void *vctx,fm1_project_work *work,uint8_t *session,br_pattern_bank *bank,uint8_t *arena,size_t capacity,br_pattern_snapshot *live,fm1_pattern_song *song){memory_source src;if(!data||!work||length>3584u||fm1_mem_overlap(data,length,work,sizeof *work)||fm1_mem_overlap(data,length,work->pattern,work->pattern?sizeof *work->pattern:0)||fm1_mem_overlap(data,length,work->session,work->session?FM1_PROJECT_SESSION:0)||fm1_mem_overlap(data,length,work->arena,work->arena_capacity)||fm1_mem_overlap(data,length,work->compressed,work->compressed_capacity)||fm1_mem_overlap(data,length,session,session?FM1_PROJECT_SESSION:0)||fm1_mem_overlap(data,length,bank,bank?sizeof *bank:0)||fm1_mem_overlap(data,length,arena,capacity)||fm1_mem_overlap(data,length,live,live?sizeof *live:0)||fm1_mem_overlap(data,length,song,song?sizeof *song:0))return FM1_PROJECT_INVALID;src.p=data;src.len=(uint32_t)length;return fm1_stream_project_decode(read_memory,&src,src.len,valid,vctx,work,session,bank,arena,capacity,live,song);}
