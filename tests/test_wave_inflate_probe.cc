#include <cstdio>
#include <cstdint>
#include "../firmware/plaits/upstream/plaits/wave_inflate_probe.h"
int main(){uint8_t in[12000],out[7756];size_t n=fread(in,1,sizeof in,stdin);uint32_t m=0;if(!wave_inflate_probe(in,n,out,sizeof out,&m))return 2;return fwrite(out,1,m,stdout)!=m;}
