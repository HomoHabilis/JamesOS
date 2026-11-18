#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "pattern_codec_source.h"
static uint8_t upload[PPS_MAX_PAYLOAD];
static br_session_t before_import;
static fm1_fixed_runtime_port port;
static int import_may_erase(void *ctx,uint32_t off) {(void)ctx;return pps_owner_may_erase(&runtime.owner[0],off)||pps_owner_may_erase(&runtime.owner[1],off);}
static void setup(void) {
 memset(&port,0,sizeof port); port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 memset(&realpark,0,sizeof realpark);init_runtime_fixture();parkops.may_erase=import_may_erase;assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"OLD")==PFS_OK);
}
int main(void) {
 static const int cuts[]={0,1,255,256,511,768,1024,1536,2048,3072,4096,8192}; size_t len; char name[PS_NAME_BYTES];
 setup();assert(fm1_codec_source_encode(fm1_project_source_read,&stream,stream.total,upload,sizeof upload,&len)==FM1_CODEC_OK);
 assert(br_capture(&before_import));memcpy(pair_before,pair_flash,sizeof pair_flash);upload[len-1]^=1;
 assert(fm1_fixed_runtime_import(&runtime,1,"IMPORT",upload,len)!=PFS_OK);assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));upload[len-1]^=1;
 assert(fm1_fixed_runtime_import(&runtime,1,"IMPORT",upload,len)==PFS_OK);assert(selected_slot==0);assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));assert(fm1_fixed_runtime_scan(&runtime,1,name)==PFS_OK&&!strcmp(name,"IMPORT"));
 for(unsigned i=0;i<sizeof cuts/sizeof cuts[0];i++) {setup();assert(br_capture(&before_import));memcpy(pair_before,pair_flash,sizeof pair_flash);pair_budget=cuts[i];(void)fm1_fixed_runtime_import(&runtime,1,"IMPORT",upload,len);pair_budget=-1;assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));assert(selected_slot==0);assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));}
 for(unsigned i=0;i<sizeof cuts/sizeof cuts[0];i++) { setup();assert(fm1_fixed_runtime_import(&runtime,1,"IMPORT",upload,len)==PFS_OK);assert(br_capture(&before_import));memcpy(pair_before,pair_flash,sizeof pair_flash);pair_budget=cuts[i];(void)fm1_fixed_runtime_import(&runtime,1,"REPLACE",upload,len);pair_budget=-1;assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_runtime_scan(&runtime,1,name)==PFS_OK);assert(!strcmp(name,"IMPORT")||!strcmp(name,"REPLACE")); }
 puts("import: validation, readback, live/other slots preserved, initial and replacement interruption");return 0;
}
