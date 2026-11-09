#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define FELUCCA_ID "FM-1_9262014"
#include "../firmware/src/ota.c"
static uint8_t *pkg,*flash, frame[640]; static uint32_t plen,flen,frame_n,now; static int pending,erasures;
static int ota_wire_send(const uint8_t *p,uint32_t n){uint8_t d[640],a[530],wire[640]; uint32_t z=ota_unpack7(p+1,n-2,d,sizeof d); if(z<7||d[2]!=0x30)return 0; uint32_t off=ota_rd32(d+7),len=ota_rd16(d+11); printf("request off=0x%x len=%u\n", off, len); if(len>512)return -1; memcpy(a,d+6,8); if(off==0xE0000000u && len==8) memcpy(a+8,"success",8); else { if(off>plen || len>plen-off)return -1; memcpy(a+8,pkg+off,len); } uint32_t s=0;for(uint32_t i=0;i<len+8;i++)s+=a[i];uint8_t msg[530];msg[0]=0;msg[1]=0x59;msg[2]=0x30;msg[3]=(len+8)&255;msg[4]=(len+8)>>8;msg[5]=0;memcpy(msg+6,a,len+8);msg[14+len]=(uint8_t)~s;frame_n=ota_pack7(msg,len+15,wire);memcpy(frame,wire,frame_n);pending=1;return 0;}
static int ota_frame_get(const uint8_t **p,uint32_t *n){if(!pending)return 0;*p=frame;*n=frame_n;return 1;}
static void ota_frame_done(void){pending=0;} static uint32_t ota_now_ms(void){return now++;} static void ota_idle(void){}
static int ota_erase(uint32_t off){if(off+4096>flen)return -1;memset(flash+off,0xff,4096);erasures++;return 0;}static int ota_prog(uint32_t off,const void *p,uint32_t n){if(off+n>flen)return -1;memcpy(flash+off,p,n);return 0;}
static int ota_fread(uint32_t off,void *p,uint32_t n){if(off+n>flen)return -1;memcpy(p,flash+off,n);return 0;}
static void ota_show(uint32_t s,int32_t code){printf("stage %u code %d\n",s,code);}static int ota_commit(const uint8_t *p){return 0;}
static uint8_t *readfile(const char *name,uint32_t *len){FILE *f=fopen(name,"rb");if(!f)exit(2);fseek(f,0,SEEK_END);*len=ftell(f);rewind(f);uint8_t *p=malloc(*len);if(fread(p,1,*len,f)!=*len)exit(2);fclose(f);return p;}
int main(int argc,char **argv){uint32_t n;uint8_t *raw=readfile(argv[1],&n);pkg=malloc(n);for(int i=0;i<20;i++)memcpy(pkg+i*47,raw+i*48,47);memcpy(pkg+940,raw+960,n-960);plen=n-20;flash=readfile(argv[2],&flen);int r=ota_stage();printf("ota_stage=%d staging_erases=%d\n",r,erasures);return 0;}
