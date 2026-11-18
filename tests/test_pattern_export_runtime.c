#define FM1_CUT_FIXTURE 1
#include "test_pattern_fixed_runtime.c"
#include "pattern_codec_source.h"
#include "pattern_sha256.h"
static uint8_t upload[PPS_MAX_PAYLOAD];
static br_session_t before_import;
static fm1_fixed_runtime_port port;
static int import_may_erase(void *ctx,uint32_t off) {(void)ctx;return pps_owner_may_erase(&runtime.owner[0],off)||pps_owner_may_erase(&runtime.owner[1],off);}
static void setup(void) {
 memset(&port,0,sizeof port); port.begin=port_begin;port.set_mode=port_mode;port.end=port_end;port.fence=port_fence;port.faulted=port_faulted;port.pair_state=port_state;port.certificate=port_cert;port.activate=port_activate;port.commit_slot=port_slot;port.role[0]=1;port.role[1]=2;port.role[2]=3;port.role[3]=4;port.named_slots=12;port.fixed_partition_auto=1;mutable_port=&port;
 memset(&realpark,0,sizeof realpark);init_runtime_fixture();parkops.may_erase=import_may_erase;assert(fm1_fixed_runtime_bind(&runtime,&port)==PFS_OK);assert(fm1_fixed_bridge_save(&runtime.bridge,0,"OLD")==PFS_OK);
}
int main(void) {
 uint32_t length=0; char name[PS_NAME_BYTES]; uint8_t snapshot[PPS_MAX_PAYLOAD];
 setup(); assert(br_capture(&before_import)); memcpy(pair_before,pair_flash,sizeof pair_flash);
 assert(fm1_fixed_runtime_export(&runtime,0,snapshot,sizeof snapshot,&length,name)==PFS_OK);
 assert(length && length<=PPS_MAX_PAYLOAD && !strcmp(name,"OLD"));
 assert(!memcmp(pair_before,pair_flash,sizeof pair_flash)); assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
 assert(fm1_fixed_runtime_export(&runtime,12,snapshot,sizeof snapshot,&length,name)==PFS_INVALID);
 assert(fm1_fixed_runtime_export(&runtime,1,snapshot,sizeof snapshot,&length,name)==PFS_EMPTY);
 assert(fm1_fixed_runtime_export(&runtime,0,snapshot,1,&length,name)==PFS_INVALID);
 assert(fm1_fixed_runtime_import(&runtime,1,"COPY",snapshot,length)==PFS_OK);
 { uint8_t copy[PPS_MAX_PAYLOAD];uint32_t n;char nm[PS_NAME_BYTES];assert(fm1_fixed_runtime_export(&runtime,1,copy,sizeof copy,&n,nm)==PFS_OK);assert(n==length&&!memcmp(copy,snapshot,n)&&!strcmp(nm,"COPY")); }

 { fm1_pattern_sha256 sha;uint8_t hash[32],wrong[32];unsigned erased;
   assert(!fm1_pattern_sha256_init(&sha)&&!fm1_pattern_sha256_update(&sha,snapshot,length)&&!fm1_pattern_sha256_final(&sha,hash));
   memcpy(wrong,hash,32);wrong[0]^=1;memcpy(pair_before,pair_flash,sizeof pair_flash);erased=raw_erases;
   assert(fm1_fixed_runtime_delete_experiment(&runtime,1,"COPY",wrong,0)==PFS_INVALID);
   assert(raw_erases==erased&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
   assert(fm1_fixed_runtime_delete_experiment(&runtime,1,"COPY",hash,0)==PFS_OK);
   assert(fm1_fixed_runtime_export(&runtime,1,snapshot,sizeof snapshot,&length,name)==PFS_EMPTY);
   assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));
   assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));
   assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
   /* Model the damaged older half of this slot after an interrupted erase. */
   assert(fm1_fixed_runtime_import(&runtime,1,"COPY",snapshot,length)==PFS_OK);
   pair_flash[3u*PS_SECTOR+100u]=0x42;memcpy(pair_before,pair_flash,sizeof pair_flash);erased=raw_erases;
   assert(fm1_fixed_runtime_delete_experiment(&runtime,1,"COPY",hash,0)==PFS_INVALID);
   assert(raw_erases==erased&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
   assert(fm1_fixed_runtime_delete_experiment(&runtime,1,"COPY",wrong,1)==PFS_INVALID);
   assert(raw_erases==erased&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
   assert(fm1_fixed_runtime_delete_experiment(&runtime,1,"COPY",hash,1)==PFS_OK);
   assert(fm1_fixed_runtime_export(&runtime,1,snapshot,sizeof snapshot,&length,name)==PFS_EMPTY);
   assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));
   assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));
   assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
   /* The separate unavailable-slot flow fingerprints both sectors and will
    * never erase a readable project or an altered pair. */
   assert(fm1_fixed_runtime_import(&runtime,1,"COPY",snapshot,length)==PFS_OK);
   {uint8_t raw_digest[32],bad_raw[32];
    assert(fm1_fixed_runtime_inspect_unreadable_experiment(&runtime,1,raw_digest)==PFS_INVALID);
    pair_flash[2u*PS_SECTOR]|=1u;memcpy(pair_before,pair_flash,sizeof pair_flash);erased=raw_erases;
    assert(fm1_fixed_runtime_inspect_unreadable_experiment(&runtime,1,raw_digest)==PFS_OK);
    assert(raw_erases==erased&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
    memcpy(bad_raw,raw_digest,32);bad_raw[0]^=1;
    assert(fm1_fixed_runtime_clear_unreadable_experiment(&runtime,1,bad_raw)==PFS_INVALID);
    assert(raw_erases==erased&&!memcmp(pair_before,pair_flash,sizeof pair_flash));
    assert(fm1_fixed_runtime_clear_unreadable_experiment(&runtime,1,raw_digest)==PFS_OK);
    assert(fm1_fixed_runtime_export(&runtime,1,snapshot,sizeof snapshot,&length,name)==PFS_EMPTY);
    assert(!memcmp(pair_before,pair_flash,2u*PS_SECTOR));
    assert(!memcmp(pair_before+4u*PS_SECTOR,pair_flash+4u*PS_SECTOR,sizeof pair_flash-4u*PS_SECTOR));
    assert(br_capture(&target_factory)&&!memcmp(&before_import,&target_factory,sizeof before_import));
   }
 }
 puts("export: validated snapshot, empty and invalid slots, live state and flash preserved");return 0;
}
