#!/usr/bin/env python3
"""Exercise the actual loader completion branch with mocked reset hooks."""
from pathlib import Path
import subprocess, sys, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
import host_target   # product: BR_HOST_TARGET (default fm1)
root=Path(__file__).resolve().parents[1]
smk=host_target.target()=='smk37'
s=host_target.select_product((root/'firmware/loader/loader.c').read_text())
ids={'fm1':'#define FELUCCA_ID "ota-FM-1_904"','smk37':'#define FELUCCA_ID "ota-SMK-37 Pro_9260001"'}
assert ids['smk37' if smk else 'fm1'] in s and ids['fm1' if smk else 'smk37'] not in s
if smk:  # SMK-37 Pro: only a verified update reboots; unknown flash keeps the loader running
    start=s.index('if (rc == 0) {', s.index('rc = ldr_session();'))
else:
    start=s.index('if (rc == 0 || rc == LDR_EFLASH)')
opening=s.index('{',start); depth=0
for end in range(opening,len(s)):
    if s[end]=='{':depth+=1
    elif s[end]=='}':
        depth-=1
        if not depth: break
branch=s[start:end+1]
fm1_source='''#include <assert.h>\n#include <setjmp.h>\n#include <stdio.h>\n#define LDR_EFLASH (-14)\nstatic int p33,core,detached,delay; static jmp_buf env;\nstatic void fm1_delay_ms(int n){delay+=n;}\nstatic void usb_detach(void){detached++;}\nstatic void fm1_reboot(void){p33++;longjmp(env,1);}\nstatic void fm1_core_reset(void){core++;longjmp(env,1);}\nstatic void apply(int rc){'''+branch+'''}\nint main(void){\n if(!setjmp(env))apply(0); assert(p33==1 && core==0 && detached==1 && delay==230);\n p33=core=detached=delay=0; if(!setjmp(env))apply(LDR_EFLASH);\n assert(p33==0 && core==1 && detached==1 && delay==230);\n p33=core=detached=delay=0; apply(-13);\n assert(!p33 && !core && !detached && !delay);\n puts("PASS: verified update uses P33 reset; unknown flash and failed update retain original paths");\n}\n'''
smk_source='''#include <assert.h>\n#include <stdio.h>\n#define LDR_EFLASH (-14)\nstatic int reset,detached,delay;\nstatic void fm1_delay_ms(int n){delay+=n;}\nstatic void usb_detach(void){detached++;}\nstatic void fm1_reboot(void){reset++;}\nstatic void apply(int rc){'''+branch+'''}\nint main(void){\n apply(0); assert(reset==1 && detached==1 && delay==230);\n reset=detached=delay=0; apply(LDR_EFLASH); apply(-13);\n assert(!reset && !detached && !delay);\n puts("PASS: only successful updates reboot from the loader");\n}\n'''
source=smk_source if smk else fm1_source
with tempfile.TemporaryDirectory(prefix='fm1-loader-restart-') as d:
    c=Path(d)/'test.c';exe=Path(d)/'test';c.write_text(source)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
