#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../firmware/plaits/upstream/plaits/wave_inflate_probe.h"
int main(){uint8_t in[8192],out[7760];uint32_t rng=0x812ba013u;for(unsigned t=0;t<30000;t++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;unsigned n=rng%8193u;for(unsigned i=0;i<n;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;in[i]=(uint8_t)rng;}std::memset(out,0xa5,sizeof out);uint32_t count=0;bool ok=wave_inflate_probe(in,n,out+2,7756,&count);if((ok&&count>7756)||out[0]!=0xa5||out[1]!=0xa5||out[7758]!=0xa5||out[7759]!=0xa5)return 1;}std::puts("PASS: 30000 bounded arbitrary streams");}
