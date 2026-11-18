#define FM1_DELETE_INTEROP_SERVER 1
#include "test_pattern_delete_protocol_runtime.c"
#include <ctype.h>
static int hexn(int c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
int main(void){pdg_gate gate={0};pdp_ops protocol={0,review_delete,perform_delete};char line[512];uint8_t body[128],reply[65];unsigned n,len;uint32_t now=1;int a,b,cmd;setvbuf(stdout,0,_IONBF,0);setup();
 while(fgets(line,sizeof line,stdin)){if(line[0]=='Q')break;a=hexn(line[0]);b=hexn(line[1]);if(a<0||b<0){puts("-");continue;}cmd=(a<<4)|b;len=0;for(unsigned i=3;line[i]&&line[i]!='\n';i+=2){a=hexn(line[i]);b=hexn(line[i+1]);if(a<0||b<0||len>=sizeof body){len=0;break;}body[len++]=(uint8_t)((a<<4)|b);}
  if(cmd==0xe0){reply[0]='P';reply[1]='D';reply[2]='L';reply[3]=1;n=4;}
  else if(!pdp_dispatch(&gate,&protocol,(uint8_t)cmd,body,len,now++,reply,&n)){puts("-");continue;}
  for(unsigned i=0;i<n;i++)printf("%02x",reply[i]);putchar('\n');
 }
 return 0;
}
