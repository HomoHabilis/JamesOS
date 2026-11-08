"""Test the actual 0x9D branch with small host stubs, without hardware."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'firmware/src/instrument_diagnostic.c').read_text()
a = s.index('        if(n) {', s.index('} else if (cmd == 0x9Du'))
b = s.index('\n#else', a)
body = s[a:b]
prefix = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#define FM1_PATTERN_PANEL_TEST 1
#define BR_PAGE_PS 10
struct { unsigned buttons,notes; } fm1_in;
struct { unsigned page,previous_keys; } br_ui;
struct { unsigned mode,action,slot,current,pending,cursor; char error[3],name[13]; } es;
struct { unsigned state; } es_directory[12];
struct { uint16_t used; uint8_t active,queued; } br_ps_state;
unsigned br_project_diag_down,br_project_diag_up,br_pattern_diag_key,br_recording,br_playing;
int br_project_diag_knob1,br_project_diag_knob2;
int br_black_key(unsigned k) {return k==1 || k==3;}
int run(const uint8_t *body, unsigned n, uint8_t *reply, unsigned *reply_n) {
'''
test = r'''
return 1; }
int main(void) {
 uint8_t r[32],q[6]={'B','R',1,4,1,0};unsigned n=0;
 br_ui.page=BR_PAGE_PS;br_ps_state.active=3;br_ps_state.queued=255;br_ps_state.used=0x401;
 assert(run(q,6,r,&n)&&br_pattern_diag_key==2);
 assert(!run(q,6,r,&n)); q[3]=5;assert(!run(q,6,r,&n));
 br_ui.previous_keys=2;q[3]=6;q[4]=0;assert(run(q,6,r,&n)&&n==17&&r[12]==1&&r[13]==4&&r[14]==3&&r[15]==255&&r[16]==3);
 q[3]=5;q[4]=1;assert(run(q,6,r,&n)&&!br_pattern_diag_key);
 br_ui.previous_keys=0;q[3]=4;q[4]=0;assert(!run(q,6,r,&n));
 q[4]=1;fm1_in.notes=1;assert(!run(q,6,r,&n));fm1_in.notes=0;es.mode=1;assert(!run(q,6,r,&n));
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d);(p/'test.c').write_text(prefix+body+test)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('0x9D pattern command guards and status passed')
