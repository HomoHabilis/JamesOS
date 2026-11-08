"""Full-bank restore simulation on initially blank storage only."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from fm1_project_full_backup import SIZE,PAIR_SIZE,pack_backup,unpack_backup
from fm1_project_recovery_model import begin_blank_restore,restore_blank_step

source=bytes(((i*17+3)&255) if i//4096 not in (4,7,20) else 255 for i in range(SIZE))
archive=pack_backup(source,'FM-1_TEST');blank=b'\xff'*SIZE
r=begin_blank_restore(archive,blank,'sacrificial-simulator')
complete,ok=restore_blank_step(archive,blank,r,'sacrificial-simulator');assert ok and complete==source
assert unpack_backup(pack_backup(complete,'FM-1_TEST'))[1]==source
# Test each flash-page interval, boundaries around the working pair and EOF.
total=sum(v!=255 for v in source)
cuts=sorted(set(range(0,total+1,256))|{1,255,257,total-1,total})
for cut in cuts:
 partial,ok=restore_blank_step(archive,blank,r,'sacrificial-simulator',cut)
 assert ok==(cut>=total)
 restored,done=restore_blank_step(archive,partial,r,'sacrificial-simulator')
 assert done and restored==source
 if cut<sum(x!=255 for x in source[:-PAIR_SIZE]):assert partial[-PAIR_SIZE:]==blank[-PAIR_SIZE:]
for foreign in [bytes([0])+blank[1:],blank[:-1]+bytes([0])]:
 try:begin_blank_restore(archive,foreign,'sacrificial-simulator');raise AssertionError('used target accepted')
 except ValueError:pass
try:restore_blank_step(archive,blank,r,'different-target');raise AssertionError('wrong target accepted')
except ValueError:pass
wrong=bytearray(blank);wrong[0]=0
try:restore_blank_step(archive,bytes(wrong),r,'sacrificial-simulator');raise AssertionError('incompatible bytes accepted')
except ValueError:pass
bad=bytearray(archive);bad[-1]^=1
try:restore_blank_step(bytes(bad),blank,r,'sacrificial-simulator');raise AssertionError('corrupt archive accepted')
except ValueError:pass
print(f'PASS: full 26-sector blank-media restore, {len(cuts)} interrupted writes resumed exactly; incompatible targets denied; working pair last')
