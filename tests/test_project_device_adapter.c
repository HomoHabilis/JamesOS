#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FL_IN(o,n,l,h) ((uint32_t)(o)>=(l) && (uint32_t)(o)<=(h) && (uint32_t)(n)<=(h)-(uint32_t)(o))
typedef struct { uint8_t data[3484]; } br_session_t;
static br_session_t br_pending_save,br_loaded,current;
static uint8_t flash[0x100000], flash_ok=1;
static int br_playing, quiet=1, fxquiet=1, blocked, tx, park, failpark, failresume;
static int erase_count,fail_erase_at;
static int br_engine_quiet(void) { return quiet; }
static int br_fx_quiet(void) { return fxquiet; }
static int br_session_save_blocked(void) { return blocked; }
static int br_quiescent_transaction_begin(void) { if(tx)return 0; tx=1;return 1; }
static void br_quiescent_transaction_end(void) { assert(tx);tx=0; }
static int br_engine_pipeline_uncontained_fault_get(void) { return 0; }
static int br_capture(br_session_t *s) { assert(tx);*s=current;return 1; }
static int br_restore(const br_session_t *s) { assert(tx);current=*s;return 1; }
static int br_session_v12_valid(const br_session_t *s) { return s->data[0]==0x42; }
static int br_session_v13_valid(const br_session_t *s) { return s->data[0]==0x43; }
static int br_session_v14_valid(const br_session_t *s) { return s->data[0]==0x44; }
static int br_session_v15_valid(const br_session_t *s) { return s->data[0]==0x44; }
static int br_session_v16_valid(const br_session_t *s) { return s->data[0]==0x45; }
static int flash_worker_park(void) { if(failpark)return -1;assert(!park);park=1;return 0; }
static int flash_worker_resume(void) { assert(park);park=0;return failresume?-1:0; }
static int st_read(uint32_t off,void *dst,uint32_t n) { assert(tx); memcpy(dst,flash+off,n);return 0; }
static int fl_erase4k(uint32_t off,uint32_t *took) {
    assert(tx && park);*took=1;
    if(++erase_count==fail_erase_at)return -1;
    memset(flash+off,0xff,4096);return 0;
}
static int fl_write(uint32_t off,const uint8_t *src,uint32_t n) {
    assert(tx && park); for(uint32_t i=0;i<n;i++){ assert((flash[off+i]&src[i])==src[i]);flash[off+i]&=src[i]; } return 0;
}
#include "../firmware/src/instrument_project_device.c"
int main(void) {
    ps_info info; memset(flash,0xff,sizeof flash); memset(&current,0x34,sizeof current);current.data[0]=0x42;
    assert(br_project_save(0,"FIRST",0,&info)==PS_OK && !tx && !park);
    current.data[3]=0x91;
    assert(br_project_load(0,&info)==PS_OK && current.data[3]==0x34 && !tx && !park);
    tx=1; int slot=-2; assert(br_project_recover(&slot)==PS_OK && slot==0 && current.data[3]==0x34);tx=0;
    br_session_t factory=current; factory.data[3]=0x81;
    assert(br_project_init(&factory)==PS_OK && current.data[3]==0x81);
    tx=1; assert(br_project_recover(&slot)==PS_OK && slot==-1 && current.data[3]==0x81);tx=0;
    assert(br_project_load(0,&info)==PS_OK && current.data[3]==0x34);
    /* If the named project commits but the working copy cannot commit, the
     * running session and the previous working copy remain intact. */
    current.data[3]=0x55;erase_count=0;fail_erase_at=2;
    assert(br_project_save(1,"SECOND",0,&info)==PS_IO && current.data[3]==0x55 && !tx && !park);
    fail_erase_at=0;tx=1;
    assert(ps_load(&br_project_store,1,&br_loaded,&info)==PS_OK && br_loaded.data[3]==0x55);
    assert(br_project_recover(&slot)==PS_OK && slot==0 && current.data[3]==0x34);tx=0;
    /* Failed working-copy commits must not apply LOAD or INIT in RAM. */
    erase_count=0;fail_erase_at=1;
    assert(br_project_load(1,&info)==PS_IO && current.data[3]==0x34 && !tx && !park);
    erase_count=0;
    assert(br_project_init(&factory)==PS_IO && current.data[3]==0x34 && !tx && !park);
    fail_erase_at=0;
    br_playing=1; assert(br_project_save(0,"NO",0,&info)==PS_INVALID);br_playing=0;
    blocked=1; assert(br_project_load(0,&info)==PS_INVALID);blocked=0;
    failpark=1; assert(br_project_save(0,"NO",0,&info)==PS_IO && !tx && !park);failpark=0;
    failresume=1; assert(br_project_save(0,"NO",0,&info)==PS_IO && !tx && !park);failresume=0;
    assert(br_project_load(0,&info)==PS_OK && current.data[3]==0x34);
    assert(br_project_erase(0,BR_PROJECT_BASE-4096)==-1);
    assert(br_project_erase(0,BR_PROJECT_END)==-1);
    current.data[0]=0x44; current.data[3]=0x27;
    assert(br_project_save(2,"RETRIG",0,&info)==PS_OK);
    current.data[3]=0;
    assert(br_project_load(2,&info)==PS_OK && current.data[0]==0x44 && current.data[3]==0x27);
    current.data[0]=0x45; current.data[3]=0x67;
    assert(br_project_save(3,"MIDI",0,&info)==PS_OK);
    current.data[3]=0;
    assert(br_project_load(3,&info)==PS_OK && current.data[0]==0x45 && current.data[3]==0x67);
    puts("PASS: device adapter quiescence, bounds, worker failures and restore");return 0;
}
