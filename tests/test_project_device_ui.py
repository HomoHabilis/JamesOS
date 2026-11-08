"""Exercise the device UI and store with an isolated in-memory NOR chip."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from run_mod_target_tests import FLAGS

with tempfile.TemporaryDirectory(prefix='fm1-device-ui-') as tmp:
    tmp = Path(tmp)
    objects = list((ROOT/'build/host/braids').glob('*.o')) + list((ROOT/'build/host/plaits_reference').glob('*.o'))
    subprocess.run(['cc', *FLAGS, '-c', 'firmware/src/audio_fixed8_plan_unit.c', '-o', str(tmp/'planner.o')], cwd=ROOT, check=True)
    subprocess.run(['c++', '-x', 'c', *FLAGS, '-DDINGHY_DEVICE_SAVE_TEST', 'tools/ui_emulator.c', '-x', 'none',
                    str(tmp/'planner.o'), *map(str, objects), '-o', str(tmp/'app')], cwd=ROOT, check=True)
    proc = subprocess.Popen([str(tmp/'app'), str(tmp/'no-initial-session.bin'), str(tmp/'session.bin')],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    def command(text):
        proc.stdin.write((text+'\n').encode());proc.stdin.flush()
        line=proc.stdout.readline()
        if not line: raise RuntimeError('device UI simulator exited')
        state=json.loads(line); left=240*240*3
        while left:
            block=proc.stdout.read(left)
            if not block: raise RuntimeError('truncated frame')
            left-=len(block)
        return state
    def snapshot():
        assert command('s')['sessionSaved']
        return (tmp/'session.bin').read_bytes()
    def button(n): return command('b '+str(n))
    def stage(n):
        state=command('t 20')
        assert state['saveStage']==n, state
        return state
    command('e 2 1')  # Start from a deterministic, non-factory sound.
    original=snapshot()
    command('n 0 1');state=button(2)  # FX retains playable keys.
    assert state['page']==2 and any(v!=0 for v in state['ledLevels'][14:])
    command('n 0 0');button(8)
    for _ in range(12):command('t 250')  # Let the released voice and effects settle.
    assert snapshot()==original
    # A failed directory read can be retried without rebooting the instrument.
    command('f 3');button(9);command('t 20');command('f 0');button(9)
    state=button(9)
    assert state['saveStage']==3 and state['saveError']=='RETRYING SLOT', state
    name_state=button(9)
    assert name_state['saveStage']==4 and name_state['ledLevels'][9]==3 and name_state['ledLevels'][0]==3, name_state
    moved=command('e 3 2')  # KNOB2 moves the cursor along the name.
    assert moved['saveCursor']==2 and moved['saveName']==name_state['saveName'], moved
    edited=command('e 2 1') # KNOB1 changes the highlighted character.
    assert edited['saveCursor']==2 and edited['saveName']!=moved['saveName'] and edited['saveName'][:2]==moved['saveName'][:2], edited
    assert button(8)['saveStage']==0
    assert button(9)['saveStage']==1
    command('t 20')
    assert button(9)['saveStage']==3
    assert button(9)['saveStage']==4
    assert button(9)['saveStage']==6
    command('f 1'); state=button(9)
    assert state['saveStage']==4 and state['saveError']=='SAVE FAILED' and snapshot()==original, state
    command('f 0');command('t 20')
    assert button(9)['saveStage']==6
    state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==0, state
    assert snapshot()==original
    command('e 2 1')
    assert snapshot()!=original
    state=command('z')
    assert state['saveCurrent']==0 and snapshot()==original, state
    # Save a second version into slot 2.
    command('e 2 1'); second=snapshot()
    button(9); command('t 20'); command('e 2 1'); button(9); command('e 2 1'); button(9)
    assert button(9)['saveStage']==6
    state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==1, state
    assert snapshot()==second
    # Save the changed current project before loading slot 1.
    command('e 2 1');changed_second=snapshot()
    button(9);command('t 20');command('e 2 2');button(9);command('e 2 -1')
    assert button(9)['saveStage']==2
    state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==0 and snapshot()==original, state
    # Load the saved second project, discarding the current state.
    button(9);command('t 20');command('e 2 2');button(9);command('e 2 1');button(9)
    command('e 2 1');state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==1 and snapshot()==changed_second, state
    # Return to slot 1 for the quick-save check.
    button(9);command('t 20');command('e 2 2');button(9);command('e 2 -1');button(9)
    command('e 2 1');state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==0 and snapshot()==original, state
    assert command('z')['saveCurrent']==0 and snapshot()==original
    # REC+SAVE works immediately after reboot without opening the slot list.
    command('e 2 1'); quick=snapshot();assert quick!=original
    command('h 13 1');state=button(9);state=command('h 13 0')
    assert state['saveStage']==0 and state['recording']==0 and state['saveCurrent']==0, state
    assert command('z')['saveCurrent']==0 and snapshot()==quick
    # INIT and reboot restore the initialized, unassigned working state.
    button(9);command('t 20');command('e 2 3');button(9);command('e 2 1')
    state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==-1, state
    initialized=snapshot();assert initialized!=original
    assert command('z')['saveCurrent']==-1 and snapshot()==initialized
    # An unassigned current project must not be saved over the pending load
    # target; choosing a different slot may proceed after overwrite approval.
    button(9);command('t 20');command('e 2 2');button(9);button(9)
    assert button(9)['saveStage']==3  # SAVE CURRENT -> pick a save slot.
    assert button(9)['saveStage']==4
    assert button(9)['saveStage']==6
    assert button(9)['saveStage']==5  # Occupied target asks for confirmation.
    state=button(9)
    assert state['saveStage']==5 and state['saveError']=='CHOOSE DIFFERENT SLOT', state
    assert snapshot()==initialized
    assert button(0)['saveStage']==4
    assert button(0)['saveStage']==3
    command('e 2 1');assert button(9)['saveStage']==4
    assert button(9)['saveStage']==6
    assert button(9)['saveStage']==5
    state=button(9)
    assert state['saveStage']==0 and state['saveCurrent']==0 and snapshot()==quick, state
    assert command('h 13 1')['recording']==0
    assert command('h 13 0')['recording']==1
    command('h 13 1');assert command('h 13 0')['recording']==0
    # Populate all twelve device slots and recover them after a reboot.
    snapshots=[]
    for slot in range(12):
        command('e 2 1');snapshots.append(snapshot())
        button(9);command('t 20');command('e 2 1');assert button(9)['saveStage']==3
        if slot:command('e 2 1')
        assert button(9)['saveStage']==4
        assert button(9)['saveStage']==6
        state=button(9)
        if state['saveStage']==5:state=button(9)  # existing slots require confirmation
        assert state['saveStage']==0 and state['saveCurrent']==slot, (slot,state)
    assert command('z')['saveCurrent']==11 and snapshot()==snapshots[11]
    for slot in (0,5,11):
        current=command('t 1')['saveCurrent']
        button(9);command('t 20');command('e 2 2');button(9)
        command('e 2 '+str(slot-current));assert button(9)['saveStage']==2
        command('e 2 1');state=button(9)
        assert state['saveStage']==0 and state['saveCurrent']==slot and snapshot()==snapshots[slot], (slot,state)
    proc.stdin.close();assert proc.wait(timeout=5)==0
    print('PASS: device SAVE UI, save/load/init, recovery, and REC gesture against simulated NOR')
