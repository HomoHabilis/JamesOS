#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "project_delete_protocol.h"
#include "pattern_sha256.h"
#define FM1_PATTERN_RAW_EXPORT_EXPERIMENT 1
#define br_ps_target_fixed_runtime runtime
#define br_engine_quiet() 1
#define br_fx_quiet() 1
static uint32_t br_import_state,br_export_nonce,br_diagnostic_upload_length,br_diagnostic_snapshot_valid,br_diagnostic_chunks[4];
static struct { uint8_t b[PPS_MAX_PAYLOAD]; } br_diagnostic_session;
static struct { unsigned state; } es_directory[12];
static void es_storage_feedback(const char *s){(void)s;}
#include "raw_delete_callbacks.inc"
static uint32_t crc32_test(const uint8_t *p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void change_generation(void){uint32_t c;pair_flash[28]++;c=crc32_test(pair_flash,88);for(unsigned i=0;i<4;i++)pair_flash[88+i]=(uint8_t)(c>>(8*i));}
static fm1_fixed_runtime_port port;
static int may_erase(void *ctx,uint32_t off){(void)ctx;return pps_owner_may_erase(&runtime.owner[0],off)||pps_owner_may_erase(&runtime.owner[1],off);}
static void setup(void){FILE *f;memset(&port,0,sizeof port);port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;memset(&realpark,0,sizeof realpark);init_runtime_fixture();parkops.may_erase=may_erase;assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);f=fopen("tests/fixtures/project-v16-store.bin","rb");assert(f&&fread(pair_flash,1,8192,f)==8192);fclose(f);}
int main(void){pdg_gate g={0};pdp_ops protocol={0,br_delete_review,br_delete_perform};uint8_t req[63]={1},reply[65];unsigned n,erases;
 setup();memcpy(pair_before,pair_flash,sizeof pair_flash);req[17]=PDG_RAW;
 assert(pdp_dispatch(&g,&protocol,PDP_REVIEW,req,18,1,reply,&n)&&n==65&&!memcmp(reply+20,"OLD SONG",8));assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 memcpy(req+18,reply+20,13);memcpy(req+31,reply+33,32);
 change_generation();erases=raw_erases;assert(pdp_dispatch(&g,&protocol,PDP_COMMIT,req,63,2,reply,&n)&&n==20&&reply[16]&&raw_erases==erases);memcpy(pair_flash,pair_before,2u*PS_SECTOR);
 memset(&g,0,sizeof g);memset(req,0,sizeof req);req[0]=2;req[17]=PDG_RAW;
 assert(pdp_dispatch(&g,&protocol,PDP_REVIEW,req,18,3,reply,&n)&&n==65);memcpy(req+18,reply+20,13);memcpy(req+31,reply+33,32);
 assert(pdp_dispatch(&g,&protocol,PDP_COMMIT,req,63,4,reply,&n)&&n==20&&!reply[16]);erases=raw_erases;assert(pdp_dispatch(&g,&protocol,PDP_COMMIT,req,63,5,reply,&n)&&raw_erases==erases);
 assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,sizeof pair_flash-2u*PS_SECTOR));
 setup();memset(&g,0,sizeof g);pair_flash[PS_SECTOR+100]=0x42;erases=raw_erases;memset(req,0,sizeof req);req[0]=3;req[17]=PDG_RAW;assert(pdp_dispatch(&g,&protocol,PDP_REVIEW,req,18,6,reply,&n)&&n==20&&reply[16]&&raw_erases==erases);
 setup();memset(pair_flash,0xff,2u*PS_SECTOR);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"CURRENT")==PFS_OK);memset(&g,0,sizeof g);memset(req,0,sizeof req);req[0]=4;req[17]=PDG_RAW;erases=raw_erases;assert(pdp_dispatch(&g,&protocol,PDP_REVIEW,req,18,7,reply,&n)&&n==20&&reply[16]&&raw_erases==erases);
 puts("raw delete: explicit review, changed pair refusal, confirmed delete, retry, damaged sibling and current project passed");return 0;}
