# SPDX-License-Identifier: GPL-3.0-only
"""Simulation-only two-image provisioning transition; never opens hardware.

No minimal device image, independent embedded-anchor inspector, or verified
physical backend exists yet. This module refuses any backend not explicitly
marked simulated_only and is not an install command.
"""
import hashlib
import json
import os
from pathlib import Path
import tempfile
from dataclasses import dataclass
try:
    from fm1_pattern_provision import ProvisionError, _read_selected
    from fm1_pattern_identity import source_generation
    from fm1_install import load_package
except ImportError:
    from tools.fm1_pattern_provision import ProvisionError, _read_selected
    from tools.fm1_pattern_identity import source_generation
    from tools.fm1_install import load_package


@dataclass(frozen=True,repr=False)
class _PrivateImage:
    data: bytes
    sha256: str
    product: str
    loader: str
    role: str
    def __repr__(self):return '<private image sha256='+self.sha256+'>'

@dataclass(frozen=True,repr=False)
class TransitionPlan:
    pin_path: str
    pin_sha256: str
    provisioner: _PrivateImage
    final: _PrivateImage
    def __repr__(self):return '<private transition plan; payloads redacted>'


def _package(path, role, pin, validator, inspector):
    path=Path(path)
    if path.is_symlink() or not path.is_file():raise ProvisionError('package must be a regular non-symlink file')
    data=path.read_bytes();digest=hashlib.sha256(data).hexdigest()
    # Validator and inspector examine the same immutable, private snapshot that
    # would be handed to a future backend; no mutable path is used afterward.
    with tempfile.TemporaryDirectory(prefix='fm1-transition-check-') as d:
        snapshot=Path(d)/'image.fwsc';snapshot.write_bytes(data);snapshot.chmod(0o600)
        product,loader,_=validator(snapshot,False)
        info=inspector(snapshot)
    expected={'role':role,'domain':pin['domain'],'generation':pin['generation'],
              'secret_sha256':hashlib.sha256(bytes.fromhex(pin['secret'])).hexdigest(),
              'package_sha256':digest,'product':product,'loader':loader}
    if info!=expected:raise ProvisionError('independent embedded-anchor/package evidence does not match')
    return _PrivateImage(data=data,sha256=digest,product=product,loader=loader,role=role)


def plan(pin_path,provisioner,final_image,repository,inspector=None,validator=load_package):
    """Validate both complete packages before creating any simulated update.

    The inspector must independently parse each compiled image and prove the
    embedded domain, source generation and secret digest. No such production
    inspector is implemented; omitting it rejects the plan. Source fingerprint
    does not attest the compiler/SDK or binary and a copied firmware image is
    not a unique hardware identity.
    """
    if inspector is None:raise ProvisionError('independent embedded-anchor inspector unavailable')
    pin=_read_selected(pin_path)
    if source_generation(repository)!=pin['generation']:raise ProvisionError('source snapshot differs from pin')
    first=_package(provisioner,'provisioner',pin,validator,inspector)
    final=_package(final_image,'final',pin,validator,inspector)
    if first.product!=final.product or first.loader!=final.loader or first.sha256==final.sha256:
        raise ProvisionError('incompatible or indistinguishable transition packages')
    pin_digest=hashlib.sha256(json.dumps(pin,sort_keys=True,separators=(',',':')).encode('ascii')).hexdigest()
    return TransitionPlan(pin_path=os.path.abspath(pin_path),pin_sha256=pin_digest,provisioner=first,final=final)


def advance(plan,backend):
    """Advance one simulated step; callers may re-enter after a power cut.

    Backend must implement an independently observed, exclusive session with
    one selected USB topology and a stable simulated flash model. No existing
    MIDI backend is allowed here. A loader resume additionally needs an exact
    immutable pending role/package/loader match. Foreign, ambiguous, stale or
    mismatched state always stops before a write. No erase outside app updater
    or root creation is permitted by this model.
    """
    if getattr(backend,'simulated_only',False) is not True:raise ProvisionError('physical transition backend is not qualified')
    if not isinstance(plan,TransitionPlan):raise ProvisionError('invalid transition plan')
    pin=_read_selected(plan.pin_path)
    if hashlib.sha256(json.dumps(pin,sort_keys=True,separators=(',',':')).encode('ascii')).hexdigest()!=plan.pin_sha256:
        raise ProvisionError('private pin changed during transition')
    a=plan.provisioner;b=plan.final
    for image in (a,b):
        if hashlib.sha256(image.data).hexdigest()!=image.sha256:raise ProvisionError('package snapshot changed')
    with backend.transaction() as tx:
        devices=tx.observed_devices()
        if not isinstance(devices,list) or len(devices)!=1:raise ProvisionError('exactly one selected connection required')
        d=devices[0]
        if not isinstance(d,dict) or any(d.get(k)!=pin[k] for k in ('vendor','product','topology')):raise ProvisionError('connection changed')
        if tx.non_destructive_only() is not True:raise ProvisionError('data-sector erasure is not authorized')
        phase=d.get('phase')
        if phase=='base':
            if d.get('generation')!=pin['generation'] or d.get('firmware_product')!=a.product:raise ProvisionError('unexpected base firmware')
            tx.install('provisioner',a.data,a.sha256,a.loader);return 'provisioner-requested'
        if phase=='loader':
            role=d.get('pending_role');image=a if role=='provisioner' else b if role=='final' else None
            if image is None or d.get('loader')!=image.loader or d.get('pending_sha256')!=image.sha256 or tx.certify_pending(image.role,image.sha256) is not True:
                raise ProvisionError('loader resume identity is ambiguous')
            tx.resume(image.role,image.data,image.sha256,image.loader);return 'resume-requested'
        if phase not in ('provisioner','final') or d.get('embedded_domain')!=pin['domain'] or d.get('generation')!=pin['generation'] or d.get('readback_verified') is not True:
            raise ProvisionError('embedded identity is not verified')
        image=a if phase=='provisioner' else b
        if d.get('package_sha256')!=image.sha256 or d.get('firmware_product')!=image.product:
            raise ProvisionError('running image is not the pinned package')
        root=tx.root_state(pin)
        if phase=='provisioner' and root=='blank':
            # The fake backend must model its durable one-shot pin/intent and
            # blank-only root write. A failure can never authorize an erase.
            if tx.provision_once(pin) is not True:raise ProvisionError('simulated root commit failed')
            return 'root-committed'
        if root!='match':raise ProvisionError('root is missing, foreign or ambiguous')
        if phase=='provisioner':
            tx.install('final',b.data,b.sha256,b.loader);return 'final-requested'
        return 'complete'
