"""Exercise SAVE against isolated emulator files, never the user's slots."""
import os
import shutil
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import tools.ui_emulator as emu

emu.build()
with tempfile.TemporaryDirectory(prefix='fm1-save-test-') as temp:
    root = Path(temp)
    out = root / 'build/ui-emulator'
    out.mkdir(parents=True)
    shutil.copy(emu.OUT / 'app', out / 'app')
    emu.OUT = out
    os.chdir(root)
    app = emu.App(emu.ROOT.parent / 'results/ui-1502-final-session/session.bin')

    def command(s):
        return app.command(s)

    def snapshot():
        assert command('s')['sessionSaved']
        return (out / 'session.bin').read_bytes()

    assert command('t 2')['recording'] == 0
    for button, page in ((2, 2), (3, 6), (4, 1), (5, 7), (6, 8),
                         (7, 3), (8, 0), (10, 5), (11, 4)):
        assert command('b 9')['saveScreen']
        state = command('b ' + str(button))
        assert not state['saveScreen'] and state['page'] == page
        if page != 0:
            command('b 8')
    command('b 9')
    command('b 1')
    command('b 1')  # Name editor, not just the main save page.
    state = command('b 2')
    assert not state['saveScreen'] and state['page'] == 2
    command('b 8')
    state = command('b 9')
    assert state['saveScreen'] and state['ledLevels'][0] == 0 and state['ledLevels'][1] == 3
    assert state['ledLevels'][8] == 0 and state['ledLevels'][12] == 0 and state['ledLevels'][9] == 3
    command('e 2 2')  # KNOB1 selects INIT.
    assert command('b 1')['saveScreen']  # OCT+ enters its confirmation.
    assert command('b 0')['saveScreen']  # OCT- returns to the list.
    assert command('b 0')['saveScreen']  # OCT- is inactive on the main menu.
    assert not command('b 12')['saveScreen']  # PLAY cancels the menu.
    command('b 9')
    command('b 1')  # SAVE PROJECT slot list.
    assert command('e 2 -16')['saveSlot'] == 1
    assert command('e 2 16')['saveSlot'] == 12
    assert command('e 2 16')['saveSlot'] == 12
    assert command('e 2 -16')['saveSlot'] == 1
    assert not command('b 12')['saveScreen']
    assert command('h 13 1')['recording'] == 0
    assert command('h 13 0')['recording'] == 1
    command('h 13 1')
    assert command('h 13 0')['recording'] == 0
    # SAVE again from the menu starts quick-save slot selection when unassigned.
    command('b 9')
    assert command('b 9')['saveScreen']
    assert not command('b 12')['saveScreen']
    snapshots = []
    for slot in range(12):
        command('e 2 1')
        snapshots.append(snapshot())
        assert command('b 9')['saveScreen'] == 1
        command('b 1')  # Enter SAVE PROJECT and choose a slot.
        command('e 2 ' + str(1 if slot else 0))
        name_state=command('b 1')  # OCT+ opens the name editor.
        assert name_state['ledLevels'][9] == 3 and name_state['ledLevels'][12] == 3
        assert command('b 9')['saveScreen'] == 0
    assert len(list((out / 'projects').glob('slot-*.bin'))) == 12
    command('b 2');before=command('b 2')  # Preserve the FX subpage.
    assert command('b 9')['saveScreen']
    quick=command('b 9')
    assert not quick['saveScreen'] and (quick['page'],quick['screen'])==(before['page'],before['screen'])
    assert quick['messageActive']
    command('b 8')
    command('h 13 1')
    command('b 9')
    assert command('h 13 0')['recording'] == 0
    assert len(list((out / 'projects').glob('slot-12-*.bin'))) == 2
    command('b 9')
    command('e 2 1')
    command('b 1')  # Enter LOAD PROJECT and choose a slot.
    command('e 2 -11')
    command('b 1')
    command('e 2 1')
    assert command('b 9')['saveScreen'] == 0
    assert snapshot() == snapshots[0]
    command('b 9')
    command('e 2 2')
    command('b 1')
    command('e 2 1')
    assert command('b 9')['saveScreen'] == 0
    assert snapshot() != snapshots[0]
    command('b 9')
    assert command('b 12')['saveScreen'] == 0
    app.proc.stdin.close()
    assert app.proc.wait(timeout=5) == 0
    print('PASS: 12 slots, load, init, cancel, REC gestures and two copies')
