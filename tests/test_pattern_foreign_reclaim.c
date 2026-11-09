#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
int main(void) {
 fm1_fixed_runtime_port port={0};char name[PS_NAME_BYTES];br_session_t before;
 port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 pair_flash[31]=0x42;pair_flash[PS_SECTOR+55]=0x55;pair_flash[24u*PS_SECTOR+31]=0x42;pair_flash[25u*PS_SECTOR+55]=0x55;
 memcpy(pair_before,pair_flash,sizeof pair_flash);assert(br_capture(&before));
 assert(fm1_fixed_bridge_recover(&runtime.bridge)==FM1_PROJECT_FOREIGN_WORKING);
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_INVALID);
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 assert(fm1_fixed_bridge_load(&runtime.bridge,0)!=PFS_OK);
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"REPLACED")==PFS_OK);
 assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_OK&&!strcmp(name,"REPLACED"));
 assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,22u*PS_SECTOR));
 assert(fm1_fixed_bridge_recover(&runtime.bridge)==PFS_OK);
 assert(fm1_fixed_bridge_load(&runtime.bridge,0)==PFS_OK);
 /* A valid survivor is kept while its incompatible sibling is replaced. */
 init_runtime_fixture();memset(&realpark,0,sizeof realpark);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"FIRST")==PFS_OK);
 pair_flash[PS_SECTOR+12]=0x31;memcpy(pair_before,pair_flash,sizeof pair_flash);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"SECOND")==PFS_OK);
 assert(!memcmp(pair_before,pair_flash,PS_SECTOR));
 assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_OK&&!strcmp(name,"SECOND"));
 /* A valid named project remains loadable when working data is foreign. */
 memset(pair_flash+24u*PS_SECTOR,0x42,2u*PS_SECTOR);
 memcpy(pair_before,pair_flash,2u*PS_SECTOR);
 assert(fm1_fixed_bridge_recover(&runtime.bridge)==FM1_PROJECT_FOREIGN_WORKING);
 assert(fm1_fixed_bridge_load(&runtime.bridge,0)==PFS_OK);
 assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));
 /* A torn first replacement stays recognizable as incompatible and can be
  * explicitly retried after a fresh startup; other slots remain untouched. */
 init_runtime_fixture();memset(&realpark,0,sizeof realpark);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 pair_flash[31]=0x42;pair_flash[PS_SECTOR+55]=0x55;memcpy(pair_before,pair_flash,sizeof pair_flash);
 pair_budget=1;assert(fm1_fixed_bridge_save(&runtime.bridge,0,"TORN")!=PFS_OK);pair_budget=-1;
 assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,22u*PS_SECTOR));
 memset(&realpark,0,sizeof realpark);memset(&runtime,0,sizeof runtime);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_INVALID);
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"RETRY")==PFS_OK);
 assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_OK&&!strcmp(name,"RETRY"));
 init_runtime_fixture();memset(&realpark,0,sizeof realpark);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 pair_flash[31]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);fail_project_read=1;
 assert(fm1_fixed_bridge_save(&runtime.bridge,0,"FAIL")!=PFS_OK);fail_project_read=0;
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash));
 /* An optional private flash capture exercises the actual foreign bank. */
 { const char *path=getenv("FM1_FOREIGN_FLASH_FIXTURE");if(path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0x97000,SEEK_SET));init_runtime_fixture();memset(&realpark,0,sizeof realpark);assert(fread(pair_flash,1,sizeof pair_flash,f)==sizeof pair_flash);fclose(f);memcpy(pair_before,pair_flash,sizeof pair_flash);assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_recover(&runtime.bridge)==FM1_PROJECT_FOREIGN_WORKING);assert(fm1_fixed_runtime_scan(&runtime,0,name)==PFS_INVALID);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"REAL FOREIGN")==PFS_OK);assert(!memcmp(pair_before+2u*PS_SECTOR,pair_flash+2u*PS_SECTOR,22u*PS_SECTOR));assert(fm1_fixed_bridge_load(&runtime.bridge,0)==PFS_OK);}}
 puts("foreign working recovery, named replacement, reload, scope and read failure passed");return 0;
}
