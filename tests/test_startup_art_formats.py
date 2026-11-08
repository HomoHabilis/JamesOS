#!/usr/bin/env python3
"""Compile the real intro decoder with small and large generated palettes."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
from generate_startup_art import encode
s=(root/'firmware/src/instrument_ui.c').read_text()
a=s.index('static void br_intro_frame(void)');b=s.index('static void br_draw_body(void)',a)
function=s[a:b]
with tempfile.TemporaryDirectory() as td:
 d=Path(td)
 for ncolors in (1,31,32,33,255,256):
  # Every run length, explicit palette coverage, then a long run crossing strips.
  pixels=[]
  for count in range(1,256): pixels.extend([(count-1)%ncolors]*count)
  pixels += list(range(ncolors))
  pixels += [ncolors-1] * (57600-len(pixels))
  palette,stream,short=encode(pixels)
  assert short == (ncolors<=32)
  p='''#include <stdint.h>\n#include <assert.h>\n#include <stdio.h>\n'''
  p+=f'#define BR_STARTUP_ART_SHORT_RUNS {int(short)}u\n#define C_BLACK 0u\n'
  p+='static const uint16_t br_startup_art_palette[]={' + ','.join(map(str,palette))+'};\n'
  p+='static const uint8_t br_startup_art_rle[]={' + ','.join(map(str,stream))+'};\n'
  p+='static uint16_t cv_px[9600],expected[57600]; static unsigned strips;\n'
  p+='static uint16_t swap16(uint16_t v){return (uint16_t)((v>>8)|(v<<8));}\n'
  p+='static void cv_begin(unsigned w,unsigned h,unsigned c){(void)c;assert(w==240&&h==40);}\n'
  p+=f'''static void lcd_blit(unsigned x,unsigned y,unsigned w,unsigned h,const uint16_t *px) {{
   assert(x==0&&y==strips*40&&w==240&&h==40);
   for(unsigned i=0;i<9600;i++) {{unsigned pos=strips*9600+i;assert(px[i]==swap16(expected[pos]));}}
   strips++;
  }}\n'''
  p+=function+f'''\nint main(void){{unsigned pos=0;for(unsigned c=1;c<=255;c++)for(unsigned j=0;j<c;j++)expected[pos++]=(c-1)%{ncolors}u;for(unsigned i=0;i<{ncolors}u;i++)expected[pos++]=i;while(pos<57600u)expected[pos++]={ncolors-1}u;br_intro_frame();assert(strips==6);return 0;}}\n'''
  (d/'test.c').write_text(p)
  subprocess.run(['cc','-std=c11','-O2','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(d/'test.c'),'-o',str(d/'test')],check=True)
  subprocess.run([str(d/'test')],check=True)
 print('actual decoder: 1/31/32/33/255/256-color formats and strip boundaries passed under ASan/UBSan')
