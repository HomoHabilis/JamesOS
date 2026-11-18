#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 or an SMK-37 Pro over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  fm1_install.py --info [--port NAME]

The device is the one whose model (FM-1, SMK-37 Pro) matches the package: every
candidate port answers the identity handshake first, and a package is never
offered to a device of the other model. --port picks the port: a port whose
name is exactly NAME (ignoring case and a trailing Windows port number) wins
over ports that merely contain NAME, so "--port FM-1" means the FM-1 even when
an SMK-37 Pro port called "FM-1 JamesOS" is also connected. --info lists every
device that answers.

If the device is still in update mode (an earlier install was cut off), the
install finishes the write. Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package, or
several devices of the package's model (choose one with --port), 3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, another identity after the install, or the
FM-1 is in another firmware's update mode. An update-mode device is resumed only
when its loader identity exactly matches the loader independently verified in
that package; the app and loader may use different numeric identities.
"""
import argparse
import os
import queue
import re
import sys
import time
import struct

try:
    import validate_package
except ImportError:  # imported as tools.fm1_install by the test suite
    from tools import validate_package

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
PORT_RE = re.compile(r"smk|fm-1|jamesos|felucca|ota|composite|sinco|usb-midi", re.I)   # never probe other gear

# seconds; the tests shorten them
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0}

EXIT = {"usage": 2, "badpkg": 2, "ambiguous": 2, "notfound": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4,
        "noloader": 5, "noreturn": 5, "mismatch": 6, "foreign": 6}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def parse_request(pkt):
    """-> (flags, addr, len) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = d[6:33].rstrip(b"\0").decode("latin-1")
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def product_of(raw):
    """the package identity ("FM-1_906"): one marker byte after each of the first 20 blocks"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw):
    """the image the device reads during an update: the package without the 20 marker bytes"""
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    """one MIDI in/out pair with a SysEx queue"""

    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return list(dict.fromkeys(self.mido.get_input_names()))

    def output_names(self):
        return list(dict.fromkeys(self.mido.get_output_names()))

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


def _port_base(name):
    """a port name without case and without the number Windows appends to each port"""
    return re.sub(r"\s+\d+$", "", name).casefold()


def port_matches(port, names):
    """-> (exact, partial): the ports named exactly `port` (ignoring case and a Windows port
    number), and the other ports whose names contain it. An exact match wins."""
    want = port.casefold()
    exact = [n for n in names if n.casefold() == want or _port_base(n) == want]
    partial = [n for n in names if n not in exact and want in n.casefold()]
    return exact, partial


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)     # Windows numbers each port
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port
        self.prefer = None

    def candidates(self, only_port, prefer=None):
        """port names to try, in order. With --port: the exact match(es), else the ports
        containing the name; with only_port=False (waiting for the loader or the restart)
        the other device-like ports follow, since a port may be renamed meanwhile."""
        names = self.backend.input_names()
        if self.port:
            exact, partial = port_matches(self.port, names)
            mine = exact or partial
            if only_port:
                order = mine
            else:
                order = exact + partial + [n for n in names if n not in exact and n not in partial and PORT_RE.search(n)]
        else:
            order = [n for n in names if PORT_RE.search(n)]
        if prefer in order:                 # the port the device was on
            order = [prefer] + [n for n in order if n != prefer]
        return order

    def _open(self, name, outs):
        out = pair_output(name, outs)
        if not out:
            return None
        try:
            link = self.backend.open(name, out)
        except Exception:
            return None
        ident = handshake(link, 2)
        if ident:
            return Device(link, ident, name)
        link.close()
        return None

    def find(self, want=None, only_port=True, prefer=None):
        """the first matching port that answers the handshake (and want(identity))"""
        outs = self.backend.output_names()
        for name in self.candidates(only_port, prefer):
            dev = self._open(name, outs)
            if dev and (want is None or want(dev.id)):
                return dev
            if dev:
                dev.link.close()
        return None

    def find_all(self):
        """every candidate port that answers the handshake (links open; the caller closes them)"""
        outs = self.backend.output_names()
        return [d for d in (self._open(n, outs) for n in self.candidates(True)) if d]

    def pick(self, product):
        """the one connected device of the package's model; refuses the other model"""
        devs = self.find_all()
        if not devs:
            raise not_found(self)
        mine = [d for d in devs if model_of(d.id.text) == model_of(product)]
        for d in devs:
            if d not in mine or len(mine) > 1:
                d.link.close()
        listing = ", ".join(f"{d.id.text} ({d.name})" for d in devs)
        if not mine:
            raise InstallError("model", f"the package is for {product}, but the connected device is not a "
                                        f"{model_of(product)}: {listing}. Nothing was written.")
        if len(mine) > 1:
            raise InstallError("ambiguous", f"several {model_of(product)} devices answer ({listing}): "
                                            "choose one with --port. Nothing was written.")
        return mine[0]

    def wait_for(self, want, secs, prefer=None):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False, prefer=prefer)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost)"""
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            if n > MAXDATA or addr + n > len(image):
                raise InstallError("badreq", f"the device asked for {addr:#x}+{n}, outside the package")
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr:addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr + n)

    def check(self, dev, image, loader_identity, step):
        """step 1: the running firmware checks the package and starts the loader"""
        step("start", dev.id.text)
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                            lambda k, end: step("check", k))
        dev.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the FM-1 {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests; nothing was written")
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.text.casefold() == loader_identity.casefold(),
                            DELAY["wait_loader"])
        if not ota:
            raise InstallError("noloader", "the update loader did not appear. Replug the USB cable and "
                                           "run the install again: the FM-1 stays in update mode until it is done")
        return ota

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package, then restarts"""
        step("write", 0)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, end: step("write", min(99, end * 100 // len(image))))
        ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        # the restarted app of the package's model (another connected model never counts);
        # a different identity of that model is reported below as a mismatch
        back = self.wait_for(lambda i: not i.loader and model_of(i.text) == model_of(product),
                             DELAY["wait_reboot"], self.prefer)
        if not back:
            raise InstallError("noreturn", "the FM-1 did not come back: power-cycle it")
        back.link.close()
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the FM-1 reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def install(self, dev, image, product, loader_identity, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write."""
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        if dev.id.loader and dev.id.text.casefold() != loader_identity.casefold():
            dev.link.close()           # another firmware's loader: its image layout may differ
            raise InstallError("foreign", f"the FM-1 is in the update mode of another firmware ({dev.id.text}): "
                                          "finish that update with its own updater (M-UPGRADE), then install again")
        ota = dev if dev.id.loader else self.check(dev, image, loader_identity, step)
        self.write(ota, image, step)
        self.prefer = getattr(dev, "name", None)     # look for the restarted app on this port first
        return self.verify(product, step)


# ------------------------------------------------------------------- CLI ---

def load_package(path, _force=False):
    try:
        with open(path, "rb") as package_file:
            raw = package_file.read()
    except OSError as e:
        raise InstallError("badpkg", f"cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"(FM-1|SMK-37 Pro)_[0-9]+", product):
        raise InstallError("badpkg", f"{path}: not an FM-1 or SMK-37 Pro package (identity {product!r})")
    try:
        validation = validate_package.validate(raw, expected_identity=product)
    except (ValueError, OSError, UnicodeError, struct.error) as e:
        raise InstallError("badpkg", f"{path}: package validation failed ({e}); nothing was written. "
                                     "Download it again.") from e
    loader_identity = validation.get("loader_identity")
    if not isinstance(loader_identity, str) or not re.fullmatch(r"ota-" + re.escape(model_of(product)) + r"_[0-9]+", loader_identity):
        raise InstallError("badpkg", f"{path}: independent validation did not identify the embedded loader; "
                                     "nothing was written.")
    image = logical_image(raw)
    bad = package_damage(image)
    if bad:
        raise InstallError("badpkg", f"{path}: damaged package ({bad}); nothing was written. "
                                     "Download it again.")
    return product, loader_identity, image


def _crc16(data, crc=0):
    """CRC-16/XMODEM, as the package and the device use"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _dec(b):
    """JieLi ENC stream cipher (its own inverse)"""
    out, key = bytearray(b), 0xFFFF
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def package_damage(image):
    """what is wrong with the update image (header, file list, each file's CRC), or None; the device
    only checks the loader before it rewrites the firmware, so a damaged file is caught here"""
    if len(image) < 0x40:
        return "too short"
    hdr = _dec(image[0:0x40])
    if _crc16(hdr[2:0x40]) != int.from_bytes(hdr[0:2], "little"):
        return "header CRC"
    count = int.from_bytes(hdr[8:10], "little")
    if not 1 <= count <= 8 or len(image) < 0x40 + 0x50 * count:
        return "file list"
    lst = image[0x40:0x40 + 0x50 * count]
    if _crc16(lst) != int.from_bytes(hdr[2:4], "little"):
        return "file list CRC"
    for i in range(count):
        e = _dec(lst[i * 0x50:(i + 1) * 0x50])
        dcrc = int.from_bytes(e[4:6], "little")
        off = int.from_bytes(e[8:12], "little")
        size = int.from_bytes(e[12:16], "little")
        name = e[0x40:0x50].split(b"\0")[0].decode("ascii", "replace")
        if off + size > len(image):
            return f"{name}: cut short"
        if _crc16(image[off:off + size]) != dcrc:
            return f"{name}: CRC"
    return None


def not_found(up):
    names = up.backend.input_names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 not found"
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}")


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"checking the package ({a} reads)")
        elif k == "write":
            self.put(f"writing {a:3d}%")
        else:
            self.end()
            msg = {"start": f"starting the update on {a}", "loader": "switching to update mode",
                   "reboot": "restarting", "done": f"done: the FM-1 runs {a}"}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


def run(a, backend, out, ask):
    up = Updater(backend, a.port)
    if a.info:
        devs = up.find_all()
        if not devs:
            raise not_found(up)
        for dev in devs:
            dev.link.close()
            mode = "update loader (update not finished)" if dev.id.loader else "running"
            print(f"{dev.id.text}  [{mode}]  port: {dev.name}", file=out)
        return 0
    product, loader_identity, image = load_package(a.package, a.force)
    print(f"package: {product}  ({a.package})", file=out)
    dev = up.pick(product)
    print(f"device:  {dev.id.text}  ({dev.name})" + ("  in update mode: the write will be finished" if dev.id.loader else ""),
          file=out)
    if not a.yes and not ask("Install? Do not unplug the FM-1 while writing. [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    prog = Progress(out)
    try:
        up.install(dev, image, product, loader_identity, prog)
    finally:
        prog.end()
    return 0


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of every connected device")
    ap.add_argument("--port", metavar="NAME",
                    help="MIDI port to use: its exact name, or part of it when no port has exactly that name")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    ap.add_argument("--force", action="store_true",
                    help="compatibility flag; independent package and embedded-loader validation still applies")
    a = ap.parse_args(argv)
    if bool(a.info) == bool(a.package):
        ap.error("give a PACKAGE.fwsc or --info")
    try:
        if backend is None:
            try:
                if sys.platform == 'darwin':
                    # CoreMIDI may expose distinct devices with identical names.
                    # Keep their port indices rather than collapsing the names.
                    sys.modules.setdefault('fm1_install', sys.modules[__name__])
                    from fm1_rtmidi_indexed import IndexedBackend
                    backend = IndexedBackend()
                else:
                    backend = MidoBackend()
            except ImportError:
                raise InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return run(a, backend, out, ask)
    except InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted: if the FM-1 is in update mode, run the install again to finish", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
