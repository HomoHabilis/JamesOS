#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""tools/fm1_install.py device selection and install flow with a fake MIDI backend (no MIDI port
is opened, no hardware). Simulated devices answer the identity handshake, serve the check and
write phases like the firmware and its update loader, and restart with the written identity.

Covers: an FM-1 (port "FM-1") and an SMK-37 Pro (port "FM-1 JamesOS") connected together in
either enumeration order; --port exact vs partial match; refusal of the other model; --info
listing every device; several devices of one model; the single-device paths (install, resume in
update mode, foreign loader, wrong identity after the install, no loader, not found); package
model and loader-identity checks."""
import io
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import fm1_install as fm  # noqa: E402

for k in fm.DELAY:
    fm.DELAY[k] = 0.0
fm.DELAY.update(hs=0.02, poll=0.01, idle_check=0.5, idle_write=0.5, wait_loader=0.5, wait_reboot=0.5, retry=0.01)

FM1, FM1_LDR = 'FM-1_9262153', 'ota-FM-1_904'
SMK, SMK_LDR = 'SMK-37 Pro_9260012', 'ota-SMK-37 Pro_9260001'
IMAGE = bytes(range(256)) * 8


def identity_packet(text):
    d = b'\x00\x59\x11' + b'\x00' * 3 + text.encode('latin-1').ljust(27, b'\x00') + b'\x00'
    assert len(d) == 34
    return b'\xF0' + fm.pack7(d) + b'\xF7'


def request_packet(addr, n, fl=0):
    u = bytearray(b'\x00\x59\x30\x00\x00\x00') + bytes([fl]) + addr.to_bytes(4, 'little') + n.to_bytes(3, 'little')
    u.append(~sum(u[6:14]) & 0xFF)
    return b'\xF0' + fm.pack7(bytes(u)) + b'\xF7'


class Device:
    """one simulated unit: running app or update loader, behind one MIDI port name each"""

    def __init__(self, app, loader_id, port, loader_port=None, mode='app', after=None):
        self.app, self.loader_id, self.port = app, loader_id, port
        self.loader_port = loader_port or port + ' OTA'
        self.mode = mode                 # 'app' or 'loader'
        self.after = after               # identity after the write (default: the package's)
        self.upgrades, self.served, self.written = [], 0, None
        self.handshakes = 0

    def to_loader(self):              # after its last request the app restarts into the loader
        self.mode = 'loader'

    def restart(self, package):
        self.written = package
        self.mode = 'app'
        self.app = self.after or package

    def port_name(self):
        return self.loader_port if self.mode == 'loader' else self.port

    def identity(self):
        return self.loader_id if self.mode == 'loader' else self.app

    def receive(self, pkt, link, backend):
        if pkt == fm.HS_QUERY:
            self.handshakes += 1
            link.q.append(identity_packet(self.identity()))
        elif pkt == fm.UPGRADE:
            self.upgrades.append(self.mode)
            if self.mode == 'app':       # step 1: check reads, then the loader starts
                link.q += [request_packet(0, 64), request_packet(64, 64), (request_packet(fm.FINISH_CHECK, 8), self.to_loader)]
            else:                         # step 2: the loader reads everything and restarts
                link.q += [request_packet(a, 256) for a in range(0, len(IMAGE), 256)]
                link.q.append((request_packet(fm.FINISH_WRITE, 8), lambda: self.restart(backend.package)))
        elif fm.parse_request(pkt) is None and pkt[:1] == b'\xF0':
            self.served += 1             # a response to one of our read requests


class Link:
    def __init__(self, backend, device, name):
        self.backend, self.device, self.name, self.q = backend, device, name, []
        self.closed = False

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        if self.closed or self.lost:
            return False
        self.device.receive(pkt, self, self.backend)
        return True

    def read(self, timeout):
        if not self.q:
            return None
        item = self.q.pop(0)
        if isinstance(item, tuple):     # the device acts once the host has read this packet
            item, then = item
            then()
        return item

    def drain(self):
        self.q.clear()

    def close(self):
        self.closed = True


class Backend:
    def __init__(self, *devices, other_ports=()):
        self.devices, self.other_ports, self.package, self.opened = list(devices), list(other_ports), None, []

    def input_names(self):
        return [d.port_name() for d in self.devices] + self.other_ports

    output_names = input_names

    def open(self, in_name, out_name):
        assert in_name == out_name
        self.opened.append(in_name)
        dev = next((d for d in self.devices if d.port_name() == in_name), None)
        if dev is None:
            raise OSError('not a device')
        return Link(self, dev, in_name)


def fm1(**kw):
    return Device(FM1.replace('2153', '0001'), FM1_LDR, 'FM-1', **kw)


def smk(**kw):
    return Device(SMK.replace('0012', '0001'), SMK_LDR, 'FM-1 JamesOS', loader_port='JamesOS-K OTA', **kw)


def install(backend, product, loader, *args):
    """fm1_install.main for a package of `product` -> (exit code, stdout, stderr)"""
    backend.package = product
    fm.load_package = lambda path, force=False: (product, loader, IMAGE)
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        code = fm.main(['pkg.fwsc', '--yes', *args], backend=backend, out=out)
    finally:
        sys.stderr = old
    return code, out.getvalue(), err.getvalue()


def info(backend, *args):
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        code = fm.main(['--info', *args], backend=backend, out=out)
    finally:
        sys.stderr = old
    return code, out.getvalue(), err.getvalue()


def untouched(dev):
    return not dev.upgrades and dev.written is None and dev.mode == 'app'


def test_port_matching():
    names = ['FM-1 JamesOS', 'FM-1', 'Other Synth']
    assert fm.port_matches('FM-1', names) == (['FM-1'], ['FM-1 JamesOS'])
    assert fm.port_matches('fm-1', names) == (['FM-1'], ['FM-1 JamesOS'])
    assert fm.port_matches('JamesOS', names) == ([], ['FM-1 JamesOS'])
    assert fm.port_matches('FM-1', ['FM-1 JamesOS 1', 'FM-1 0']) == (['FM-1 0'], ['FM-1 JamesOS 1'])  # Windows numbers
    up = fm.Updater(Backend(smk(), fm1()), 'FM-1')
    assert up.candidates(True) == ['FM-1']                     # exact wins: the SMK port is not tried
    assert up.candidates(False)[:2] == ['FM-1', 'FM-1 JamesOS']
    assert fm.Updater(Backend(smk(), fm1()), 'FM').candidates(True) == ['FM-1 JamesOS', 'FM-1']
    # Choosing the device: with an exact --port the substring-only port is not even asked who it is.
    # (While waiting for the loader and the restart, other device ports are asked for their identity
    # only; an UPDATE is never sent to them: see untouched() below.)
    for order in ((0, 1), (1, 0)):
        devs = [fm1(), smk()]
        up = fm.Updater(Backend(*[devs[i] for i in order]), 'FM-1')
        dev = up.pick(FM1)
        assert dev.id.text == devs[0].app and devs[1].handshakes == 0
        devs = [fm1(), smk()]
        up = fm.Updater(Backend(*[devs[i] for i in order]), 'FM-1 JamesOS')
        assert up.pick(SMK).id.text == devs[1].app and devs[0].handshakes == 0


def test_both_connected_no_port():
    for order in ((0, 1), (1, 0)):
        for product, loader, mine in ((FM1, FM1_LDR, 0), (SMK, SMK_LDR, 1)):
            devs = [fm1(), smk()]
            b = Backend(*[devs[i] for i in order])
            code, out, err = install(b, product, loader)
            assert code == 0, (order, product, err)
            assert devs[mine].written == product and devs[mine].app == product
            assert devs[mine].upgrades == ['app', 'loader']
            assert untouched(devs[1 - mine]), (order, product)
            assert f'done: the FM-1 runs {product}' in out


def test_both_connected_exact_port():
    for order in ((0, 1), (1, 0)):
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, FM1, FM1_LDR, '--port', 'FM-1')
        assert code == 0 and devs[0].written == FM1 and untouched(devs[1]), (order, err)
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, SMK, SMK_LDR, '--port', 'FM-1 JamesOS')
        assert code == 0 and devs[1].written == SMK and untouched(devs[0]), (order, err)
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, SMK, SMK_LDR, '--port', 'jamesos')   # partial match, one port
        assert code == 0 and devs[1].written == SMK and untouched(devs[0]), (order, err)


def test_wrong_model_refused():
    for order in ((0, 1), (1, 0)):
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, SMK, SMK_LDR, '--port', 'FM-1')       # exact port is the FM-1
        assert code == 6 and 'not a SMK-37 Pro' in err and untouched(devs[0]) and untouched(devs[1]), (order, err)
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, FM1, FM1_LDR, '--port', 'FM-1 JamesOS')
        assert code == 6 and untouched(devs[0]) and untouched(devs[1]), (order, err)
    d = smk()
    code, out, err = install(Backend(d), FM1, FM1_LDR)                      # only the other model
    assert code == 6 and untouched(d) and 'SMK-37 Pro_9260001' in err
    d = fm1()
    code, out, err = install(Backend(d), SMK, SMK_LDR)
    assert code == 6 and untouched(d)
    # Updater.install itself still refuses the other model (the second line of defence).
    d = smk()
    link = Backend(d).open('FM-1 JamesOS', 'FM-1 JamesOS')
    try:
        fm.Updater(None).install(fm.Device(link, fm.parse_identity(identity_packet(d.app)), d.port),
                                 IMAGE, FM1, FM1_LDR)
        raise AssertionError('other model accepted')
    except fm.InstallError as e:
        assert e.code == 'model' and link.closed and untouched(d)


def test_partial_port_matching_both_picks_model():
    for order in ((0, 1), (1, 0)):
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order])
        code, out, err = install(b, FM1, FM1_LDR, '--port', 'FM')
        assert code == 0 and devs[0].written == FM1 and untouched(devs[1]), (order, err)


def test_several_of_one_model():
    a, c = fm1(), Device('FM-1_9262000', FM1_LDR, 'FM-1 2nd')
    code, out, err = install(Backend(a, c, smk()), FM1, FM1_LDR)
    assert code == 2 and 'choose one with --port' in err and untouched(a) and untouched(c)
    code, out, err = install(Backend(a, c), FM1, FM1_LDR, '--port', 'FM-1')
    assert code == 0 and a.written == FM1 and untouched(c), err


def test_info_lists_all():
    for order in ((0, 1), (1, 0)):
        devs = [fm1(), smk()]
        b = Backend(*[devs[i] for i in order], other_ports=['IAC Driver Bus 1'])
        code, out, err = info(b)
        lines = out.strip().splitlines()
        assert code == 0 and len(lines) == 2, out
        assert {l.split()[0] for l in lines} == {'FM-1_9260001', 'SMK-37'}
        assert 'port: FM-1 JamesOS' in out and 'port: FM-1\n' in out + '\n'
        assert 'IAC Driver Bus 1' not in b.opened                 # other gear is never probed
        code, out, err = info(Backend(*[devs[i] for i in order]), '--port', 'FM-1')
        assert code == 0 and out.strip().splitlines() == ['FM-1_9260001  [running]  port: FM-1']
    code, out, err = info(Backend(fm1(mode='loader')))
    assert code == 0 and 'ota-FM-1_904  [update loader (update not finished)]  port: FM-1 OTA' in out
    code, out, err = info(Backend())
    assert code == 3 and 'not found' in err


def test_single_device_paths():
    d = fm1()
    code, out, err = install(Backend(d), FM1, FM1_LDR)
    assert code == 0 and d.app == FM1 and 'package: FM-1_9262153' in out
    d = smk()
    code, out, err = install(Backend(d), SMK, SMK_LDR)
    assert code == 0 and d.app == SMK
    # Resume: a unit left in its update loader gets the write only.
    d = fm1(mode='loader')
    code, out, err = install(Backend(d), FM1, FM1_LDR)
    assert code == 0 and d.upgrades == ['loader'] and d.app == FM1 and 'update mode' in out
    # Another firmware's loader is refused before anything is written.
    d = Device('FM-1_9260001', 'ota-FM-1_900', 'FM-1', mode='loader')
    code, out, err = install(Backend(d), FM1, FM1_LDR)
    assert code == 6 and 'update mode of another firmware' in err and not d.upgrades
    # The check phase starts a loader whose identity differs from the package's: never written.
    d = Device('FM-1_9260001', 'ota-FM-1_900', 'FM-1')
    code, out, err = install(Backend(d), FM1, FM1_LDR)
    assert code == 5 and d.upgrades == ['app'] and d.written is None
    # Written, but the unit comes back with another identity of its model: reported as a mismatch.
    d = fm1(after='FM-1_9269999')
    code, out, err = install(Backend(d), FM1, FM1_LDR)
    assert code == 6 and 'reports FM-1_9269999' in err
    # ... and another model running next to it does not count as the restarted unit.
    d, other = fm1(after='FM-1_9269999'), smk()
    code, out, err = install(Backend(other, d), FM1, FM1_LDR)
    assert code == 6 and 'reports FM-1_9269999' in err and untouched(other)
    code, out, err = install(Backend(), FM1, FM1_LDR)
    assert code == 3
    code, out, err = install(Backend(fm1()), FM1, FM1_LDR, '--port', 'nothing')
    assert code == 3 and "no MIDI port matching 'nothing'" in err


def test_package_model_checks():
    real_validate, real_damage, real_load = fm.validate_package.validate, fm.package_damage, fm.load_package
    import tempfile

    def package(identity):
        raw = bytearray(fm.BLOCKS * fm.BLK + 64)
        for i in range(fm.BLOCKS):
            raw[i * fm.BLK + fm.KEEP] = (ord(identity[i]) + i + 1) & 0xFF if i < len(identity) else 0x7D
        return bytes(raw)
    try:
        fm.package_damage = lambda image: None
        with tempfile.TemporaryDirectory() as tmp:
            for identity, loader, ok in ((FM1, FM1_LDR, True), (SMK, SMK_LDR, True), (FM1, SMK_LDR, False),
                                         (SMK, FM1_LDR, False), ('XYZ_1', 'ota-XYZ_1', False)):
                path = Path(tmp) / 'p.fwsc'
                path.write_bytes(package(identity))
                fm.validate_package.validate = lambda raw, expected_identity=None, _l=loader: {'loader_identity': _l}
                try:
                    product, ldr, image = real_load(path)
                    assert ok and product == identity and ldr == loader, (identity, loader)
                except fm.InstallError as e:
                    assert not ok and e.code == 'badpkg', (identity, loader, e)
    finally:
        fm.validate_package.validate, fm.package_damage, fm.load_package = real_validate, real_damage, real_load


if __name__ == '__main__':
    real_load = fm.load_package
    for name, fn in list(globals().items()):
        if name.startswith('test_') and callable(fn):
            fm.load_package = real_load
            fn()
            print(f'{name}: passed')
    print('fm1_install device selection, model refusal, --info and install paths passed (fake MIDI only)')
