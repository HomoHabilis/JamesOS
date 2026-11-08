#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define MQ 16u
static uint32_t mi_r, mi_w, midi_in_q[MQ], times[MQ], generations[MQ];
static uint32_t midi_last_timestamp, midi_last_generation, current_generation=1u;
static uint8_t br_midi_clock_enabled=1u;
static uint32_t br_diagnostic_restore_in_progress, br_ps_tick_claimed, fault, gate, depth;
static uint32_t seen[32], seen_time[32], count, overflow, rejected;
static uint32_t br_engine_pipeline_uncontained_fault_get(void) { return fault; }
static uint32_t br_pattern_irq_edits_blocked(void) { return gate; }
static int br_foreground_mutation_begin(uint32_t *old) { *old=depth++; return !fault && !br_diagnostic_restore_in_progress; }
static void br_foreground_mutation_end(uint32_t old) { depth=old; }
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2) {
    uint32_t i=mi_r++%MQ, p=midi_in_q[i];
    midi_last_timestamp=times[i]; midi_last_generation=generations[i];
    *st=p>>8; *d1=p>>16; *d2=p>>24; return 1;
}
static void br_midi(uint8_t st, uint8_t d1, uint8_t d2) {
    (void)d1;(void)d2; assert(depth==1u);
    if(midi_last_generation!=current_generation) { ++rejected; return; }
    if(count==3u) { ++overflow; return; }
    seen[count]=st; seen_time[count++]=midi_last_timestamp;
}
#include "../firmware/src/pattern_ui_clock_service.h"
static void push(uint8_t st,uint32_t time,uint32_t gen) {
    unsigned i=mi_w++%MQ; midi_in_q[i]=(uint32_t)st<<8;times[i]=time;generations[i]=gen;
}
int main(void) {
    push(0xf8,11,1); push(0xf8,22,0); push(0xf8,33,1); push(0xf8,44,1);
    push(0xf8,55,1); push(0xfa,66,1); push(0xf8,77,1);
    br_ui_clock_service(); assert(mi_r==4 && count==3 && rejected==1 && !depth);
    assert(seen_time[0]==11 && seen_time[1]==33 && seen_time[2]==44);
    br_ui_clock_service(); assert(mi_r==5 && overflow==1); /* same overflow behavior as normal path */
    br_ui_clock_service(); assert(mi_r==5); /* Start blocks later F8 */
    mi_r++; br_ui_clock_service(); assert(mi_r==7 && overflow==2);
    mi_r=mi_w=0; push(0xfb,1,1); push(0xfc,2,1); push(0xf2,3,1); push(0x90,4,1);
    br_ui_clock_service(); assert(mi_r==0); mi_r++; br_ui_clock_service(); assert(mi_r==1);
    mi_r++; br_ui_clock_service(); assert(mi_r==2); mi_r++; br_ui_clock_service(); assert(mi_r==3);
    mi_r=mi_w=0; push(0xf8,9,1); gate=1; br_ui_clock_service(); assert(!mi_r);
    gate=0; br_ps_tick_claimed=1; br_ui_clock_service(); assert(!mi_r); br_ps_tick_claimed=0;
    fault=1; br_ui_clock_service(); assert(!mi_r); fault=0;
    br_diagnostic_restore_in_progress=1; br_ui_clock_service(); assert(!mi_r);
    br_diagnostic_restore_in_progress=0; br_midi_clock_enabled=0; br_ui_clock_service(); assert(!mi_r);
    puts("bounded UI MIDI clock forwarding passed");
}
