# SPDX-License-Identifier: GPL-3.0-only
"""Host-only provisioning protocol model; no device transport is provided.

A future authenticated transport must implement the transaction contract
below. This module is deliberately not invoked by fm1_install.py and cannot
open a MIDI port or write a device by itself.
"""
import json
import hashlib
import hmac
import os
from pathlib import Path
import re
import secrets
import stat

BASE = 0x97000
SECTORS = 26
PIN_VERSION = 1
_IDENTITY = re.compile(r"[0-9a-f]{64}\Z")
_DOMAIN = re.compile(r"[0-9a-f]{32}\Z")

class ProvisionError(Exception):
    pass


def _check_pin(pin):
    if not isinstance(pin, dict) or set(pin) != {"version", "device", "base", "root", "peer", "domain"}:
        raise ProvisionError("invalid pin schema")
    if type(pin['version']) is not int or pin['version'] != PIN_VERSION or pin['base'] != BASE or type(pin['base']) is not int:
        raise ProvisionError("unknown pin version or store")
    if not isinstance(pin['device'], str) or not _IDENTITY.fullmatch(pin['device']):
        raise ProvisionError("device identity must be a verified 256-bit fingerprint")
    if not isinstance(pin['domain'], str) or not _DOMAIN.fullmatch(pin['domain']) or int(pin['domain'], 16) == 0:
        raise ProvisionError("invalid namespace domain")
    if any(type(pin[x]) is not int or not 0 <= pin[x] < SECTORS for x in ('root', 'peer')) or pin['root'] == pin['peer']:
        raise ProvisionError("invalid pinned root sectors")
    return pin


def _create_pin(path, pin):
    """Create once and durably sync; a failed partial pin is never removed."""
    path = Path(path)
    payload = (json.dumps(_check_pin(pin), sort_keys=True, separators=(',', ':')) + '\n').encode('ascii')
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, 'O_NOFOLLOW', 0)
    try:
        fd = os.open(path, flags, 0o600)
    except OSError as exc:
        raise ProvisionError("pin already exists or cannot be created; it was not replaced") from exc
    try:
        off = 0
        while off < len(payload):
            n = os.write(fd, payload[off:])
            if n <= 0:
                raise OSError("short pin write")
            off += n
        os.fsync(fd)
    except OSError as exc:
        raise ProvisionError("pin write failed; keep the incomplete file for inspection") from exc
    finally:
        os.close(fd)
    try:
        directory = os.open(path.parent, os.O_RDONLY | getattr(os, 'O_DIRECTORY', 0))
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
    except OSError as exc:
        raise ProvisionError("pin directory sync failed; no device write was attempted") from exc


def read_pin(path):
    flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
    try:
        fd = os.open(path, flags)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink != 1 or info.st_size > 4096:
                raise ProvisionError("unsafe pin file")
            data = b''
            while len(data) <= 4096:
                part = os.read(fd, 4097-len(data))
                if not part:
                    break
                data += part
        finally:
            os.close(fd)
        if len(data) > 4096:
            raise ProvisionError("oversized pin file")
        return _check_pin(json.loads(data.decode('ascii')))
    except (OSError, UnicodeError, ValueError) as exc:
        raise ProvisionError("pin could not be read safely") from exc


def provision(backend, pin_path, verified_device, root, peer):
    """Pin first, then commit one root in an exclusive backend transaction.

    backend.transaction() yields an object with `authenticated_device()`
    returning the *independently authenticated* 64-hex fingerprint, and
    `inspect(pin)` returning exactly 'blank', 'match', or 'unsafe'. 'blank'
    certifies both root sectors erased/reserved, no conflicting root, and
    serialization against every writer until exit. `commit(pin)` invokes the
    checked target root primitive and rereads it; no erase is permitted.

    These are strict requirements on a future backend, not something USB-MIDI
    identity strings currently establish. Errors leave the exclusive pin in
    place and never authorize reclaim. Call resume() with that same pin.
    """
    if not isinstance(verified_device, str) or not _IDENTITY.fullmatch(verified_device):
        raise ProvisionError("independent device authentication is required")
    domain = secrets.token_bytes(16)
    pin = _check_pin(dict(version=PIN_VERSION, device=verified_device, base=BASE,
                          root=root, peer=peer, domain=domain.hex()))
    with backend.transaction() as tx:
        if tx.authenticated_device() != verified_device or tx.inspect(pin) != 'blank':
            raise ProvisionError("device or blank-root preflight failed")
        _create_pin(pin_path, pin)
        if tx.commit(pin) is not True or tx.inspect(pin) != 'match':
            raise ProvisionError("root commit failed or could not be verified; keep the pin")
    return pin


def resume(backend, pin_path, verified_device):
    """Recover a pinned interrupted attempt without replacing any data."""
    pin = read_pin(pin_path)
    if verified_device != pin['device']:
        raise ProvisionError("pin belongs to a different device")
    with backend.transaction() as tx:
        if tx.authenticated_device() != verified_device:
            raise ProvisionError("device authentication failed")
        state = tx.inspect(pin)
        if state == 'match':
            return pin
        if state != 'blank':
            raise ProvisionError("root is torn, foreign or ambiguous; no write attempted")
        if tx.commit(pin) is not True or tx.inspect(pin) != 'match':
            raise ProvisionError("root commit failed or could not be verified; keep the pin")
    return pin

# A provisional, single-enumerated-unit workflow. USB topology and firmware
# generation are NOT authentication and are NOT a unique hardware identity.
# It is suitable only for a non-erasing experiment with an explicit pin; no
# production backend or installer wiring exists here.
_TOPOLOGY = re.compile(r"[A-Za-z0-9_.:/-]{1,128}\Z")
_GENERATION = re.compile(r"[0-9a-f]{64}\Z")


def _selected_pin(pin):
    keys = {'version','kind','vendor','product','topology','generation','base','root','peer','domain','secret'}
    if not isinstance(pin, dict) or set(pin) != keys or type(pin['version']) is not int or pin['version'] != 2 or pin['kind'] != 'provisional-usb-topology':
        raise ProvisionError('invalid provisional pin')
    common = dict(version=PIN_VERSION,device='00'*32,base=pin['base'],root=pin['root'],peer=pin['peer'],domain=pin['domain'])
    _check_pin(common)
    if type(pin['vendor']) is not int or type(pin['product']) is not int or pin['vendor'] != 4617 or pin['product'] != 1 or not isinstance(pin['topology'],str) or not _TOPOLOGY.fullmatch(pin['topology']) or not isinstance(pin['generation'],str) or not _GENERATION.fullmatch(pin['generation']):
        raise ProvisionError('invalid USB selection or firmware generation')
    if not isinstance(pin['secret'],str) or not re.fullmatch(r'[0-9a-f]{64}',pin['secret']) or int(pin['secret'],16)==0:
        raise ProvisionError('invalid provisioning secret')
    return pin


def _write_marker(path, data):
    """Persist a new file without overwriting; retain a partial file on error."""
    path=Path(path)
    try:
        fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL|getattr(os,'O_NOFOLLOW',0),0o600)
    except OSError as exc:
        raise ProvisionError('file already exists or cannot be created') from exc
    try:
        off=0
        while off<len(data):
            n=os.write(fd,data[off:])
            if n<=0:raise OSError('short write')
            off+=n
        os.fsync(fd)
    except OSError as exc:
        raise ProvisionError('incomplete marker retained for inspection') from exc
    finally:
        os.close(fd)
    try:
        directory=os.open(path.parent,os.O_RDONLY|getattr(os,'O_DIRECTORY',0))
        try:os.fsync(directory)
        finally:os.close(directory)
    except OSError as exc:
        raise ProvisionError('directory sync failed; no device write attempted') from exc


def _read_selected(path):
    try:
        fd=os.open(path,os.O_RDONLY|getattr(os,'O_NOFOLLOW',0))
        try:
            info=os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or info.st_size>4096 or info.st_mode & 0o077:
                raise ProvisionError('unsafe provisional pin')
            data=b''
            while len(data)<=4096:
                part=os.read(fd,4097-len(data))
                if not part:break
                data+=part
        finally:os.close(fd)
        if len(data)>4096:raise ProvisionError('oversized provisional pin')
        return _selected_pin(json.loads(data.decode('ascii')))
    except (OSError,UnicodeError,ValueError) as exc:
        raise ProvisionError('provisional pin could not be read safely') from exc


def _single_observation(tx,pin,require_domain):
    """Match one current connection. This does not identify a physical unit."""
    devices=tx.observed_devices()
    if not isinstance(devices,list) or len(devices)!=1 or not isinstance(devices[0],dict):
        raise ProvisionError('exactly one selected FM-1 connection is required')
    device=devices[0]
    if any(device.get(k)!=pin[k] for k in ('vendor','product','topology','generation')):
        raise ProvisionError('selected topology or firmware generation changed')
    if require_domain and (device.get('readback_verified') is not True or device.get('embedded_domain')!=pin['domain']):
        raise ProvisionError('firmware namespace readback does not match the pin')


def prepare_selected(backend,pin_path,topology,generation,root,peer):
    """Create a CSPRNG pin for one connection; performs no device write.

    The returned domain must be embedded into the intended per-unit firmware
    image and preserved across every update. The backend must hold its exclusive
    device transaction while enumerating and checking the root sectors.
    """
    pin=_selected_pin(dict(version=2,kind='provisional-usb-topology',vendor=4617,
        product=1,topology=topology,generation=generation,base=BASE,root=root,
        peer=peer,domain=secrets.token_bytes(16).hex(),secret=secrets.token_bytes(32).hex()))
    with backend.transaction() as tx:
        _single_observation(tx,pin,False)
        if tx.non_destructive_only() is not True or tx.inspect(pin)!='blank':
            raise ProvisionError('blank roots and a non-erasing transaction are required')
        payload=(json.dumps(pin,sort_keys=True,separators=(',',':'))+'\n').encode('ascii')
        _write_marker(pin_path,payload)
    return pin


def commit_selected(backend,pin_path):
    """Provision a previously pinned, matching firmware once; never erase.

    A durable one-shot attempt marker precedes the write. A repeated call may
    verify a matching root, but an interrupted/blank/foreign state cannot be
    automatically retried because it could refer to a different physical unit.
    This provisional topology workflow cannot authenticate a device.
    """
    pin=_read_selected(pin_path)
    with backend.transaction() as tx:
        _single_observation(tx,pin,True)
        if tx.non_destructive_only() is not True:
            raise ProvisionError('reclamation or erasure is not authorized')
        state=tx.inspect(pin)
        if state=='match':return pin
        if state!='blank':raise ProvisionError('foreign, torn or ambiguous roots')
        _write_marker(str(pin_path)+'.attempt',b'FM1-PROVISION-ATTEMPT-1\n')
        if tx.commit(pin) is not True or tx.inspect(pin)!='match':
            raise ProvisionError('root commit failed; retain the pin and attempt marker')
    return pin


def selected_hmac_response(pin_path, challenge):
    """Calculate D4 for a local pin; never print or transmit the secret.

    This is an additional per-build gate, not device authentication or
    replay protection across reboot. The caller must independently verify
    the selected connection, generation/domain and one-shot safe-state lease.
    """
    pin=_read_selected(pin_path)
    if not isinstance(challenge,bytes) or len(challenge)!=32:
        raise ProvisionError('invalid provisioning challenge')
    return hmac.new(bytes.fromhex(pin['secret']),b'FM1-PROVISION-ARM-v1'+challenge,hashlib.sha256).digest()
