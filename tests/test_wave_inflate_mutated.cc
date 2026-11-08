#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../firmware/plaits/upstream/plaits/wave_inflate_probe.h"
#define FM1_PLAITS_WAVE_BLOCK_COUNT 7
#include "../firmware/plaits/upstream/plaits/wav_integrated_waves_packed.inc"
int main(){uint8_t input[8192],out[7760];uint32_t rng=0x132729a5u;unsigned offset=0;for(unsigned block=0;block<7;block++){unsigned n=fm1_waves_block_sizes[block];if(n>sizeof input)return 1;for(unsigned k=0;k<128;k++){memcpy(input,fm1_waves_packed+offset,n);for(unsigned z=0;z<1+k%4;z++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;input[rng%n]^=(uint8_t)(1u<<((rng>>16)&7u));}memset(out,0xa5,sizeof out);uint32_t count=0;bool ok=wave_inflate_probe(input,n,out+2,7756,&count);if((ok&&count>7756)||out[0]!=0xa5||out[1]!=0xa5||out[7758]!=0xa5||out[7759]!=0xa5)return 2;}offset+=n;}std::puts("PASS: 896 mutated compressed waveform streams bounded");}
