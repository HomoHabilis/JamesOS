#!/usr/bin/env python3
"""Exercise the real session-loan guard with a retained undo image (host only)."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'firmware/src/instrument_session.c').read_text()
start=s.index('int br_pattern_session_loan_acquire('); brace=s.index('{',start); depth=0
for i in range(brace,len(s)):
    depth+=(s[i]=='{')-(s[i]=='}')
    if not depth:
        func=s[start:i+1];break
c='''#include <assert.h>\n#include <stddef.h>\n#include <string.h>\n#define FM1_SESSION_LOAN_LOADED 1u\n#define FM1_SESSION_LOAN_PENDING 2u\n#define FM1_SESSION_LOAN_UNDO 4u\ntypedef struct { unsigned char data[3484]; } br_session_t;\nstatic br_session_t br_loaded,br_pending_save,br_undo;\nstatic unsigned br_pattern_loan_mask,br_save_pending,br_undo_valid;\nstatic int blocked;\nstatic int br_pattern_session_scratch_blocked(void){return blocked;}\nstatic int quiet(void *p){return *(int *)p;}\n'''+func+'''\nint main(void){void *l=(void*)1,*p=(void*)2,*u=(void*)3; size_t n=19;int q=1;unsigned char before[sizeof br_undo];\n memset(&br_undo,0x6b,sizeof br_undo);memcpy(before,&br_undo,sizeof before);br_undo_valid=1;\n assert(!br_pattern_session_loan_acquire(7,quiet,&q,&l,&p,&u,&n));\n assert(br_undo_valid && !br_pattern_loan_mask && !memcmp(before,&br_undo,sizeof before));\n assert(l==(void*)1 && p==(void*)2 && u==(void*)3 && n==19);\n assert(br_pattern_session_loan_acquire(3,quiet,&q,&l,&p,&u,&n));\n assert(br_undo_valid && !memcmp(before,&br_undo,sizeof before) && l==&br_loaded && p==&br_pending_save && !u);\n return 0;}\n'''
with tempfile.TemporaryDirectory(prefix='fm1-loan-') as d:
 p=Path(d);(p/'test.c').write_text(c)
 for opt in ('-O0','-O2'):
  subprocess.run(['cc','-std=c11',opt,'-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
print('actual loan guard preserves retained undo and leaves outputs unchanged (host)')
