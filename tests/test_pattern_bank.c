#include "pattern_bank.h"
#include "pattern_storage_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static br_pattern_bank bank;
static br_pattern_snapshot live, work, clip, expect0, expect1, before;
static uint8_t arena[18000], backup[18000], encoded[2*sizeof(br_pattern_snapshot)+12];
static uint32_t rng=999;
static void random_data(void *p,size_t n) { uint8_t *v=p; while(n--) { rng=rng*1664525u+1013904223u; *v++=(uint8_t)(rng>>24); } }
int main(void) {
    size_t old_size; uint16_t old_used;
    memset(&live,0,sizeof live); live.length[0]=16; live.synth_note[2][37]=55;
    assert(br_pattern_bank_init(&bank,arena,sizeof arena)==BR_BANK_OK);
    assert(br_pattern_bank_select(&bank,1,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(bank.active==1 && bank.used==3);
    assert(live.synth_note[2][37]==55); expect0=live;
    live.synth_note[2][37]=78; live.kit_retrig[22][3]=17; expect1=live;
    assert(br_pattern_bank_select(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&live,&expect0,sizeof live));
    assert(br_pattern_bank_copy(&bank,1,&live,&clip)==BR_BANK_OK);
    assert(!memcmp(&clip,&expect1,sizeof clip));
    assert(br_pattern_bank_paste(&bank,2,&live,&clip,encoded,sizeof encoded)==BR_BANK_OK);
    assert(br_pattern_bank_select(&bank,2,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&live,&expect1,sizeof live));
    assert(br_pattern_bank_copy(&bank,0,&live,&clip)==BR_BANK_OK);
    assert(br_pattern_bank_paste(&bank,2,&live,&clip,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&live,&expect0,sizeof live));
    /* Preparing a queued switch reserves worst-case outgoing space, not the
     * currently small encoded value; no bank state changes on preflight. */
    before=live; old_size=bank.size; old_used=bank.used; memcpy(backup,arena,sizeof arena);
    assert(br_pattern_bank_prepare(&bank,0,&live,&work,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&work,&expect0,sizeof work));
    assert(br_pattern_bank_select(&bank,0,&live,&clip,encoded,sizeof encoded)==BR_BANK_INVALID);
    assert(br_pattern_bank_paste(&bank,1,&live,&clip,encoded,sizeof encoded)==BR_BANK_INVALID);
    assert(br_pattern_bank_cancel(&bank)==BR_BANK_OK);
    assert(br_pattern_bank_prepare(&bank,0,&live,&work,sizeof live+(sizeof live+63u)/64u+11u)==BR_BANK_FULL);
    assert(bank.active==2 && bank.size==old_size && bank.used==old_used && !memcmp(backup,arena,sizeof arena));
    assert(br_pattern_bank_select(&bank,3,&live,&work,encoded,1)==BR_BANK_FULL);
    assert(!memcmp(&before,&live,sizeof live) && bank.size==old_size && bank.used==old_used && bank.active==2);
    assert(!memcmp(backup,arena,sizeof arena));
    arena[bank.offset[0]+8]^=1; memcpy(backup,arena,sizeof arena);
    assert(br_pattern_bank_select(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_CORRUPT);
    assert(!memcmp(&before,&live,sizeof live) && bank.size==old_size && bank.used==old_used && bank.active==2);
    assert(!memcmp(backup,arena,sizeof arena)); arena[bank.offset[0]+8]^=1;
    assert(br_pattern_bank_select(&bank,0,&live,&live,encoded,sizeof encoded)==BR_BANK_INVALID);
    /* A full arena cannot lose edits or mutate earlier entries. */
    assert(br_pattern_bank_init(&bank,arena,200)==BR_BANK_OK);
    random_data(&live,sizeof live); before=live; memcpy(backup,arena,200);
    assert(br_pattern_bank_prepare(&bank,1,&live,&work,sizeof encoded)==BR_BANK_FULL);
    assert(br_pattern_bank_select(&bank,1,&live,&work,encoded,sizeof encoded)==BR_BANK_FULL);
    assert(bank.active==0 && bank.used==1 && !bank.size && !memcmp(&before,&live,sizeof live));
    assert(!memcmp(backup,arena,200));
    /* Host stress for the bound, including short literal runs separated by
     * the least-profitable zero runs and fully arbitrary byte images. */
    for(unsigned trial=0;trial<500;trial++) {
        size_t n;
        random_data(&live,sizeof live);
        if(trial%3==0) for(size_t i=0;i<sizeof live;i+=3) {
            ((uint8_t *)&live)[i]=0; if(i+1<sizeof live) ((uint8_t *)&live)[i+1]=0;
        }
        assert(fm1_pattern_codec_encode((const uint8_t *)&live,sizeof live,encoded,sizeof encoded,&n)==FM1_CODEC_OK);
        assert(n<=sizeof live+(sizeof live+63u)/64u+12u);
    }
    /* Reservation holds despite worst-pattern outgoing edits after preparation. */
    assert(br_pattern_bank_init(&bank,arena,sizeof arena)==BR_BANK_OK);
    memset(&live,0,sizeof live);
    assert(br_pattern_bank_prepare(&bank,1,&live,&work,sizeof encoded)==BR_BANK_OK);
    random_data(&live,sizeof live); before=live;
    assert(br_pattern_bank_commit(&bank,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(bank.active==1 && !bank.reserved);
    assert(br_pattern_bank_copy(&bank,0,&live,&clip)==BR_BANK_OK && !memcmp(&clip,&before,sizeof clip));
    /* Compressed clipboard lives in the same arena. If a replacement will
     * not fit, the old clipboard remains usable and every stored byte stays. */
    assert(br_pattern_bank_init(&bank,arena,200)==BR_BANK_OK);
    memset(&live,0,sizeof live); live.step[0][4]=123; before=live;
    assert(br_pattern_bank_clip_copy(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(br_pattern_bank_clip_paste(&bank,1,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(bank.used==3 && bank.clip_length);
    old_size=bank.size; memcpy(backup,arena,200);
    random_data(&live,sizeof live);
    assert(br_pattern_bank_clip_copy(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_FULL);
    assert(bank.size==old_size && !memcmp(backup,arena,200));
    assert(br_pattern_bank_clip_paste(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&live,&before,sizeof live));
    assert(br_pattern_bank_copy(&bank,1,&live,&clip)==BR_BANK_OK && !memcmp(&clip,&before,sizeof clip));
    /* Replacing the clipboard moves it behind an existing pattern; replacing
     * that earlier pattern must adjust the clipboard offset. */
    assert(br_pattern_bank_clip_copy(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(bank.clip_offset>bank.offset[1]);
    assert(br_pattern_bank_clip_paste(&bank,1,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    live.step[0][4]=777;
    assert(br_pattern_bank_clip_paste(&bank,0,&live,&work,encoded,sizeof encoded)==BR_BANK_OK);
    assert(!memcmp(&live,&before,sizeof live));
    puts("pattern bank host experiment passed"); return 0;
}
