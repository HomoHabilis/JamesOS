/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#define BR_MIDI_CLOCK_EXPERIMENT 1
#define MQ 64u
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
static uint32_t midi_in_q[MQ], midi_in_time_q[MQ], midi_in_generation_q[MQ];
static volatile uint32_t mi_w, mi_r, br_midi_clock_generation=7;
static uint32_t ticks=100;
static uint32_t fm1_ticks(void) { return ticks++; }
static uint32_t con, count, reads, writes;
static uint32_t fake_con_read(void) { return con; }
static void fake_con_write(uint32_t v) { (void)v; writes++; }
static uint32_t fake_count_read(void) { reads++; return count; }
#define FM1_UART1_CON_READ() fake_con_read()
#define FM1_UART1_CON_WRITE(v) fake_con_write(v)
#define FM1_UART1_COUNT_READ() fake_count_read()
#define FM1_UART1_SYNC() ((void)0)
#include "../firmware/src/midi_uart.c"
static uint32_t pkt(uint32_t st,uint32_t a,uint32_t b) {return (st==0xf2?3:st>=0xf8?15:st>>4)|(st<<8)|(a<<16)|(b<<24);}
static void send(const uint8_t *data, unsigned n) { for(unsigned i=0;i<n;i++) um_byte(data[i]); }
int main(void) {
 const uint8_t data[]={0x90,60,100,62,101,0xf8,64,0xfe,102,0xc1,5,6,0xf0,0x22,0x24,0x35,0x7d,0xf7,70,71,0xb0,7,127,0xf2,1,2,9,9,0x80,60,0};
 const uint32_t expected[]={pkt(0x90,60,100),pkt(0x90,62,101),pkt(0xf8,0,0),pkt(0x90,64,102),pkt(0xc1,5,0),pkt(0xc1,6,0),pkt(0xb0,7,127),pkt(0xf2,1,2),pkt(0x80,60,0)};
 send(data,sizeof data); assert(mi_w==sizeof expected/sizeof expected[0]);
 for(unsigned i=0;i<mi_w;i++){assert(midi_in_q[i]==expected[i]); assert(midi_in_generation_q[i]==7); assert(midi_in_time_q[i]==100+i);}
 con=0;count=42;assert(fm1_uart1_rx_take()==0 && reads==0 && writes==0);
 con=0x800;count=3;assert(fm1_uart1_rx_take()==3 && reads==1 && writes==2);
 uart_midi_paused=1; reads=writes=0; uart_midi_poll(); assert(reads==0 && writes==0);
 uart_midi_paused=0;
 memset(&um,0,sizeof um); mi_w=mi_r=0;
 const uint8_t stream[]={0x90,45,100,0x80,45,0}; memcpy((void*)um_ring,stream,sizeof stream);
 uart_midi_take(6); assert(um.pend==2 && mi_w==1); uart_midi_take(0); assert(um.pend==0 && mi_w==2 && midi_in_q[1]==pkt(0x80,45,0));
 mi_w=MQ;mi_r=0;midi_in_overflow=0;um_byte(0x90);um_byte(3);um_byte(4);assert(midi_in_overflow==1);
 puts("PASS: physical MIDI parsing, clock, SPP, bounded drain, DMA event and overflow");return 0;
}
