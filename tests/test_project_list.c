#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
static br_session_t before_import;
static fm1_fixed_runtime_port port;
static int import_may_erase(void *ctx,uint32_t off) {(void)ctx;return pps_owner_may_erase(&runtime.owner[0],off)||pps_owner_may_erase(&runtime.owner[1],off);}
static void setup(void) {
 memset(&port,0,sizeof port);port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 memset(&realpark,0,sizeof realpark);init_runtime_fixture();parkops.may_erase=import_may_erase;assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"OLD")==PFS_OK);
}
static uint32_t test_crc(const uint8_t *p,unsigned n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));}return ~c;}
static void test_put(uint8_t *p,uint32_t v){p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
int main(void){
 fm1_project_list_entry out[FM1_PROJECT_LIST_PAGE], before[FM1_PROJECT_LIST_PAGE];
 unsigned erases,writes; setup(); assert(br_capture(&before_import));
 memcpy(pair_before,pair_flash,sizeof pair_flash);erases=raw_erases;writes=raw_writes;
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK);
 assert(out[0].state==FM1_LIST_PRESENT&&!strcmp(out[0].name,"OLD"));
 for(unsigned i=1;i<6;i++)assert(out[i].state==FM1_LIST_EMPTY);
 assert(fm1_fixed_runtime_list(&runtime,6,out)==PFS_OK);
 for(unsigned i=0;i<6;i++)assert(out[i].state==FM1_LIST_EMPTY);
 assert(raw_erases==erases&&raw_writes==writes&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
 assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"NEW")==PFS_OK);
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK&&out[0].state==FM1_LIST_PRESENT&&!strcmp(out[0].name,"NEW"));
 test_put(pair_flash+PS_SECTOR+28,1);test_put(pair_flash+PS_SECTOR+88,test_crc(pair_flash+PS_SECTOR,88));
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK&&out[0].state==FM1_LIST_UNAVAILABLE); /* equal generations */
 memcpy(pair_flash,pair_before,sizeof pair_flash); /* restore one-copy fixture */
 pair_flash[2u*PS_SECTOR+256]=0x42; /* erased header, torn data */
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK&&out[1].state==FM1_LIST_UNAVAILABLE);
 pair_flash[PS_SECTOR+32]=0; /* unknown sibling never shown as empty */
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK&&out[0].state==FM1_LIST_UNAVAILABLE);
 pair_flash[PS_SECTOR+32]=255;pair_flash[256]^=1; /* payload CRC mismatch */
 assert(fm1_fixed_runtime_list(&runtime,0,out)==PFS_OK&&out[0].state==FM1_LIST_UNAVAILABLE);
 memset(out,0x5a,sizeof out);memcpy(before,out,sizeof out);independent_authority=0;
 assert(fm1_fixed_runtime_list(&runtime,0,out)==FM1_PROJECT_BUSY&&!memcmp(out,before,sizeof out));
 independent_authority=1;assert(fm1_fixed_runtime_list(&runtime,7,out)==PFS_INVALID);
 assert(fm1_fixed_runtime_list(&runtime,0,(fm1_project_list_entry *)(void *)&runtime)==PFS_INVALID);
 puts("project listing: CRC, erased sectors, foreign siblings, busy and read-only passed");return 0;
}
