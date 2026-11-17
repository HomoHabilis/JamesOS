#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
static fm1_fixed_runtime_port port;
int main(void) {
 uint8_t snapshot[PPS_MAX_PAYLOAD]; uint32_t length=0; char name[PS_NAME_BYTES]; FILE *f; unsigned writes,erases;
 memset(&port,0,sizeof port);port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 memset(&realpark,0,sizeof realpark);init_runtime_fixture();assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);
 f=fopen("tests/fixtures/project-v16-store.bin","rb");assert(f);assert(fread(pair_flash,1,8192,f)==8192);fclose(f);
 memcpy(pair_before,pair_flash,sizeof pair_flash);writes=raw_writes;erases=raw_erases;
 assert(fm1_fixed_runtime_export(&runtime,0,snapshot,sizeof snapshot,&length,name)!=PFS_OK);
 assert(fm1_fixed_runtime_export_raw(&runtime,0,snapshot,sizeof snapshot,&length,name)==PFS_OK);
 assert(length==1843&&!strcmp(name,"OLD SONG")&&!memcmp(snapshot,pair_flash+256,length));
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash)&&raw_writes==writes&&raw_erases==erases);
 assert(fm1_fixed_runtime_export_raw(&runtime,1,snapshot,sizeof snapshot,&length,name)==PFS_EMPTY);
 assert(fm1_fixed_runtime_export_raw(&runtime,12,snapshot,sizeof snapshot,&length,name)==PFS_INVALID);
 { uint8_t saved[FM1_PROJECT_SESSION];memcpy(saved,adapter_session,sizeof saved);
   assert(fm1_fixed_runtime_export_raw(&runtime,0,adapter_session,PPS_MAX_PAYLOAD,&length,name)==PFS_INVALID);
   assert(!memcmp(saved,adapter_session,sizeof saved)); }
 port.role[0]^=1;assert(fm1_fixed_runtime_export_raw(&runtime,0,snapshot,sizeof snapshot,&length,name)==PFS_INVALID);port.role[0]^=1;
 pair_flash[PS_SECTOR+100]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);
 assert(fm1_fixed_runtime_export_raw(&runtime,0,snapshot,sizeof snapshot,&length,name)==PFS_INVALID);
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash)&&raw_writes==writes&&raw_erases==erases);
 puts("raw export: legacy fixture, integrity refusal and zero writes passed");return 0;
}
