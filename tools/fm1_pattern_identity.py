# SPDX-License-Identifier: GPL-3.0-only
"""Build-only provisional identity helper; never opens a device."""
import hashlib
from pathlib import Path
try:
    from fm1_pattern_provision import ProvisionError, _read_selected, _write_marker
except ImportError:
    from tools.fm1_pattern_provision import ProvisionError, _read_selected, _write_marker

# This fingerprints source inputs, not the compiler/SDK or the finished binary.
# The normal build's SDK and package validation must still run independently.
BUILD_TOOLS = ('build.py','fm1pkg_make.py','lz4blk.py','check_runtime_ram.py',
               'validate_package.py','fm1_pattern_provision.py','fm1_pattern_identity.py')

def source_generation(repository):
    root=Path(repository).resolve(); firmware=root/'firmware'
    if not firmware.is_dir():raise ProvisionError('firmware source tree missing')
    files=[]
    for path in firmware.rglob('*'):
        if path.is_symlink():raise ProvisionError('symlink in firmware input tree')
        if path.is_file():files.append(path)
    for name in BUILD_TOOLS:
        path=root/'tools'/name
        if path.is_symlink() or not path.is_file():raise ProvisionError('build source input missing or linked')
        files.append(path)
    digest=hashlib.sha256();digest.update(b'FM1-PATTERN-SOURCE-TREE-v1\0')
    for path in sorted(files,key=lambda p:p.relative_to(root).as_posix()):
        name=path.relative_to(root).as_posix().encode('utf-8');data=path.read_bytes()
        digest.update(len(name).to_bytes(4,'little'));digest.update(name)
        digest.update(len(data).to_bytes(8,'little'));digest.update(hashlib.sha256(data).digest())
    return digest.hexdigest()


def write_header(pin_path, output_dir, repository):
    """Validate exact source snapshot and create a private generated header.

    Existing mismatched files are never replaced. The caller is responsible
    for keeping build artifacts private: firmware containing this secret can
    be inspected and HMAC is not physical-device authentication.
    """
    pin=_read_selected(pin_path)
    if source_generation(repository)!=pin['generation']:
        raise ProvisionError('source tree does not match the pinned generation')
    directory=Path(output_dir);directory.mkdir(parents=True,exist_ok=True)
    target=directory/'fm1_pattern_identity_generated.h'
    def octets(value):return ','.join('0x'+value[i:i+2]+'u' for i in range(0,len(value),2))
    body=('/* Private per-unit experimental build input. Never publish. */\n'
          '#define FM1_PROVISION_DOMAIN_INIT {'+octets(pin['domain'])+'}\n'
          '#define FM1_PROVISION_GENERATION_INIT {'+octets(pin['generation'])+'}\n'
          '#define FM1_PROVISION_SECRET_INIT {'+octets(pin['secret'])+'}\n'
          '#define FM1_PROVISION_ROOT_INDEX '+str(pin['root'])+'u\n'
          '#define FM1_PROVISION_PEER_INDEX '+str(pin['peer'])+'u\n').encode('ascii')
    if target.exists() or target.is_symlink():
        if target.is_symlink() or not target.is_file() or target.stat().st_mode&0o077 or target.read_bytes()!=body:
            raise ProvisionError('generated identity exists with different or unsafe contents')
    else:_write_marker(target,body)
    return target
