#!/usr/bin/env python3
"""Exercise the actual cropped decoder, including run and strip boundaries."""
from pathlib import Path
import random,subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'tools'))
from generate_startup_art import encode16
s=(root/'firmware/src/instrument_ui.c').read_text();a=s.index('static void br_intro_frame(void)');b=s.index('static void br_draw_body(void)',a);fn=s[a:b]
r=random.Random(4);pixels=[]
for count in list(range(1,256))*3:
 if len(pixels)+count > 13920:break
 pixels += [r.randrange(16)]*count
pixels += [3]*(13920-len(pixels));palette,stream=encode16(pixels)
expected=[0]*(91*240)+pixels+[0]*(91*240)
p='''#include <stdint.h>\n#include <assert.h>\n#include <string.h>\n#define BR_STARTUP_ART_CROPPED 1u\n#define BR_STARTUP_ART_TOP 91u\n#define BR_STARTUP_ART_BOTTOM 149u\n#define C_BLACK 0u\n'''
p+='static const uint16_t br_startup_art_palette[]={' + ','.join(map(str,palette))+'};\n'
p+='static const uint8_t br_startup_art_rle[]={' + ','.join(map(str,stream))+'};\n'
p+='static const uint16_t expected[]={' + ','.join(map(str,expected))+'};\n'
p+='static uint16_t cv_px[9600]; static unsigned strips;\nstatic uint16_t swap16(uint16_t v){return (uint16_t)((v>>8)|(v<<8));}\n'
p+='static void cv_begin(unsigned w,unsigned h,unsigned c){assert(w==240&&h==40&&c==0);memset(cv_px,0,sizeof cv_px);}\n'
p+='static void lcd_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *px){assert(x==0&&y==strips*40&&w==240&&h==40);for(unsigned i=0;i<9600;i++)assert(px[i]==swap16(expected[strips*9600+i]));strips++;}\n'
p+=fn+'\nint main(void){br_intro_frame();assert(strips==6);return 0;}\n'
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(p);subprocess.run(['cc','-std=c11','-O2','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],check=True);subprocess.run([str(d/'test')],check=True)
print('actual cropped decoder: run lengths and strip boundaries passed under ASan/UBSan')
