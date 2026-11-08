/* Experimental bounded raw-DEFLATE decoder, measurement only. */
#ifndef FM1_WAVE_INFLATE_PROBE_H
#define FM1_WAVE_INFLATE_PROBE_H
#include <stdint.h>
struct wave_bits { const uint8_t *p; uint32_t n, at, bits, have; };
static bool wave_get(wave_bits *s, unsigned n, uint32_t *out) {
 while(s->have<n) { if(s->at==s->n) return false; s->bits|=(uint32_t)s->p[s->at++]<<s->have; s->have+=8; }
 *out=s->bits&((1u<<n)-1u); s->bits>>=n; s->have-=n; return true;
}
struct wave_huff { uint16_t count[16], sym[288]; };
static bool wave_tree(wave_huff *h,const uint8_t *lengths,unsigned n) {
 uint16_t off[16]; int left=1; unsigned i,len; for(i=0;i<16;i++)h->count[i]=0;
 for(i=0;i<n;i++){if(lengths[i]>15)return false;h->count[lengths[i]]++;}
 if(h->count[0]==n)return false;
 for(len=1;len<=15;len++){left=(left<<1)-h->count[len];if(left<0)return false;}
 off[1]=0;for(len=1;len<15;len++)off[len+1]=off[len]+h->count[len];
 for(i=0;i<n;i++)if(lengths[i])h->sym[off[lengths[i]]++]=(uint16_t)i;
 return true;
}
static bool wave_symbol(wave_bits *s,const wave_huff *h,uint32_t *out) {
 uint32_t code=0,first=0,index=0,bit; for(unsigned len=1;len<=15;len++){
  if(!wave_get(s,1,&bit))return false;code|=bit;uint32_t count=h->count[len];
  if(code>=first && code-first<count){*out=h->sym[index+code-first];return true;}
  index+=count;first=(first+count)<<1;code<<=1;
 } return false;
}
static __attribute__((noinline)) bool wave_inflate_probe(const uint8_t *in,uint32_t n,uint8_t *out,uint32_t cap,uint32_t *written){
 static const uint8_t order[19]={16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
 static const uint16_t lb[29]={3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
 static const uint8_t le[29]={0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
 static const uint16_t db[30]={1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
 static const uint8_t de[30]={0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
 wave_bits s={in,n,0,0,0}; wave_huff lit,dist,code;uint8_t lens[320];uint32_t used=0,last=0,v,t;
 if(!in||!out||!written)return false;
 while(!last){if(!wave_get(&s,1,&last)||!wave_get(&s,2,&t))return false;
  if(t==0){s.bits=0;s.have=0;uint32_t len,inv;if(!wave_get(&s,16,&len)||!wave_get(&s,16,&inv)||(len^(inv&65535u))!=65535u||len>cap-used)return false;while(len--){if(!wave_get(&s,8,&v))return false;out[used++]=(uint8_t)v;}continue;}
  unsigned nl,nd;
  if(t==1){nl=288;nd=32;for(unsigned i=0;i<288;i++)lens[i]=i<144?8:i<256?9:i<280?7:8;for(unsigned i=0;i<32;i++)lens[288+i]=5;}
  else if(t==2){uint32_t x,y,z;if(!wave_get(&s,5,&x)||!wave_get(&s,5,&y)||!wave_get(&s,4,&z))return false;nl=x+257;nd=y+1;unsigned nc=z+4;if(nl>286||nd>32)return false;uint8_t cl[19]={0};for(unsigned i=0;i<nc;i++){if(!wave_get(&s,3,&v))return false;cl[order[i]]=(uint8_t)v;}if(!wave_tree(&code,cl,19))return false;unsigned k=0,total=nl+nd;while(k<total){if(!wave_symbol(&s,&code,&v))return false;if(v<16)lens[k++]=(uint8_t)v;else{uint32_t x,count;if(v==16){if(!k||!wave_get(&s,2,&x))return false;count=x+3;x=lens[k-1];}else if(v==17){if(!wave_get(&s,3,&x))return false;count=x+3;x=0;}else if(v==18){if(!wave_get(&s,7,&x))return false;count=x+11;x=0;}else return false;if(count>total-k)return false;while(count--)lens[k++]=(uint8_t)x;}}}
  else return false;
  if(!lens[256]||!wave_tree(&lit,lens,nl)||!wave_tree(&dist,lens+nl,nd))return false;
  for(;;){if(!wave_symbol(&s,&lit,&v))return false;if(v<256){if(used==cap)return false;out[used++]=(uint8_t)v;continue;}if(v==256)break;if(v>285)return false;v-=257;uint32_t extra,ln=lb[v];if(!wave_get(&s,le[v],&extra))return false;ln+=extra;if(!wave_symbol(&s,&dist,&v)||v>=30||!wave_get(&s,de[v],&extra))return false;uint32_t back=db[v]+extra;if(back>used||ln>cap-used)return false;while(ln--){out[used]=out[used-back];used++;}}
 }
 if(s.at!=n)return false;*written=used;return true;
}
#endif
